#pragma once

#include "mtproto/details/mtproto_abstract_socket.h"

#include <QtWebSockets/QWebSocket>

namespace MTP::details {

class WssSocket final : public AbstractSocket {
public:
	explicit WssSocket(not_null<QThread*> thread);

	void connectToHost(const QString &address, int port) override;
	bool isGoodStartNonce(bytes::const_span nonce) override;
	void timedOut() override;
	bool isConnected() override;
	bool hasBytesAvailable() override;
	int64 read(bytes::span buffer) override;
	void write(bytes::const_span prefix, bytes::const_span buffer) override;
	int32 debugState() override;
	QString debugPostfix() const override;

private:
	void fail(int code, const QString &description);

	QWebSocket _socket;
	QByteArray _incoming;
	bool _failed = false;
};

} // namespace MTP::details
