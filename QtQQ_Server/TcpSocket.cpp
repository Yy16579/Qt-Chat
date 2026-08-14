#include "TcpSocket.h"
#include "Protocol.h"

#include <QDebug>
#include <QtEndian>


TcpSocket::TcpSocket(QObject* parent)
	: QTcpSocket(parent)
	, m_descriptor(-1)
	, m_uid(-1)
{}

TcpSocket::~TcpSocket()
{}

void TcpSocket::initSocket() {
	//获取对象描述符
	this->m_descriptor = this->socketDescriptor();	

	// 连接信号槽
	connect(this, &QTcpSocket::readyRead, this, &TcpSocket::onReadyRead);
	connect(this, &QTcpSocket::disconnected, this, &TcpSocket::onClientDisconnected);
}

void TcpSocket::setUid(int uid) {
	this->m_uid = uid;
}

int TcpSocket::getUid() {
	return this->m_uid;
}

void TcpSocket::onReadyRead() {
	//	TcpSocket（网络层）：
	//		├─ 读取字节流
	//		├─ 粘包处理（缓冲拼接）
	//		├─ 拆出完整应用层协议包
	//		└─ emit signalPacketReady(完整包, fd)

	// 1. 读取所有可用数据，追加到缓冲区
	this->m_buffer.append(this->readAll());

	// 2. 循环切包：只要缓冲区里有完整包就取出
	while (m_buffer.size() >= PACKET_HEADER_SIZE) {
		// 2.1 校验魔数（偏移0，2字节，大端序）
		quint16 magic = qFromBigEndian<quint16>(m_buffer.constData());

		if (magic != PACKET_MAGIC) {
			//魔数错误：数据错位或非法数据
			//进阶处理：在缓冲区中查找下一个魔数位置，丢弃错误数据，重新对齐数据流
			static const QByteArray magicBytes = QByteArray::fromRawData("\x5A\x5A", 2);
			int nextMagicPos = m_buffer.indexOf(magicBytes, 1);

			if (nextMagicPos > 0) {
				//找到下一个魔数：丢弃错误数据，从魔数位置继续处理
				qDebug() << QStringLiteral("魔数错误，丢弃 %1 字节错误数据，从下一个魔数重新对齐")
					.arg(nextMagicPos);
				m_buffer.remove(0, nextMagicPos);
				continue;	//重新回到 while 开头，重新校验魔数
			}
			else {
				//找不到下一个魔数：全部丢弃
				qDebug() << QStringLiteral("魔数错误且未找到下一个魔数，丢弃缓冲区所有数据");
				m_buffer.clear();
				break;
			}
		}

		// 2.2 解析数据长度字段（偏移5，2字节，大端序）
		quint16 dataLength = qFromBigEndian<quint16>(m_buffer.constData() + 5);	//数据体长度
		quint16 packetSize = PACKET_HEADER_SIZE + dataLength;		//数据包总长度

		// 2.3 缓冲区数据不足一个完整包，等待下次数据到达
		if (m_buffer.size() < packetSize) {
			break;
		}

		// 2.4 取出一个完整包
		QByteArray packet = m_buffer.left(packetSize);
		m_buffer.remove(0, packetSize);

		// 2.5 完成粘包处理，发射信号
		emit this->signalPacketReady(packet, this->m_descriptor);
	}
}

void TcpSocket::onClientDisconnected() {
	emit this->signalClientDisconnected(this->m_descriptor);
}
