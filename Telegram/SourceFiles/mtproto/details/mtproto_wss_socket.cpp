#include "mtproto/details/mtproto_wss_socket.h"

#include "base/invoke_queued.h"

#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QSslConfiguration>
#include <QtWebSockets/QWebSocketHandshakeOptions>

namespace MTP::details {
namespace {

constexpr auto kPayloadLimit = 64 * 1024 * 1024;

} // namespace

WssSocket::WssSocket(not_null<QThread*> thread)
: AbstractSocket(thread)
, _socket(u"https://web.telegram.org"_q) {
	_socket.moveToThread(thread);
	_socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	_socket.setMaxAllowedIncomingFrameSize(kPayloadLimit);
	_socket.setMaxAllowedIncomingMessageSize(kPayloadLimit);

	connect(&_socket, &QWebSocket::connected, this, [=] {
		InvokeQueued(this, [=] {
			if (isConnected()) {
				_connected.fire({});
			}
		});
	});
	connect(&_socket, &QWebSocket::disconnected, this, [=] {
		InvokeQueued(this, [=] {
			if (!_failed) {
				_disconnected.fire({});
			}
		});
	});
	connect(&_socket, &QWebSocket::binaryMessageReceived, this, [=](
			const QByteArray &message) {
		if (_failed || message.isEmpty()) {
			return;
		}
		if (_incoming.size() > kPayloadLimit - message.size()) {
			fail(0, u"WSS receive queue limit exceeded"_q);
			return;
		}
		const auto notify = _incoming.isEmpty();
		_incoming.append(message);
		if (notify) {
			InvokeQueued(this, [=] {
				if (hasBytesAvailable()) {
					_readyRead.fire({});
				}
			});
		}
	});
	connect(&_socket, &QWebSocket::textMessageReceived, this, [=] {
		fail(0, u"Unexpected WSS text message"_q);
	});
	connect(&_socket, &QWebSocket::errorOccurred, this, [=](
			QAbstractSocket::SocketError error) {
		fail(int(error), _socket.errorString());
	});
}

void WssSocket::connectToHost(const QString &address, int port) {
	auto url = QUrl();
	url.setScheme(u"wss"_q);
	url.setHost(address);
	url.setPort(port);
	url.setPath(u"/apiws"_q);

	auto ssl = _socket.sslConfiguration();
	ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
	ssl.setProtocol(QSsl::TlsV1_2OrLater);
	_socket.setSslConfiguration(ssl);

	auto handshake = QWebSocketHandshakeOptions();
	handshake.setSubprotocols({ u"binary"_q });
	_socket.open(url, handshake);
}

bool WssSocket::isGoodStartNonce(bytes::const_span nonce) {
	return true;
}

void WssSocket::timedOut() {
	_socket.abort();
}

bool WssSocket::isConnected() {
	return !_failed && (_socket.state() == QAbstractSocket::ConnectedState);
}

bool WssSocket::hasBytesAvailable() {
	return !_failed && !_incoming.isEmpty();
}

int64 WssSocket::read(bytes::span buffer) {
	const auto count = std::min(buffer.size(), std::size_t(_incoming.size()));
	bytes::copy(buffer, bytes::make_span(_incoming).first(count));
	_incoming.remove(0, count);
	return count;
}

void WssSocket::write(bytes::const_span prefix, bytes::const_span buffer) {
	const auto send = [&](bytes::const_span data) {
		const auto packet = QByteArray::fromRawData(
			reinterpret_cast<const char*>(data.data()), data.size());
		return _socket.sendBinaryMessage(packet) == packet.size();
	};
	if (!isConnected()) {
		return;
	}
	const auto queued = _socket.bytesToWrite();
	if (queued < 0
		|| queued + int64(prefix.size()) + int64(buffer.size()) > kPayloadLimit
		|| (!prefix.empty() && !send(prefix))
		|| !send(buffer)) {
		fail(0, u"Could not queue WSS message"_q);
	}
}

int32 WssSocket::debugState() {
	return _socket.state();
}

QString WssSocket::debugPostfix() const {
	return u"_wss"_q;
}

void WssSocket::fail(int code, const QString &description) {
	if (_failed) {
		return;
	}
	_failed = true;
	logError(code, description);
	InvokeQueued(this, [=] {
		_socket.abort();
		_error.fire({});
	});
}

} // namespace MTP::details
