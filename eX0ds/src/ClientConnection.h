#pragma once
#ifndef __ClientConnection_H__
#define __ClientConnection_H__

class ClientConnection
	: public NetworkConnection
{
public:
	ClientConnection(SOCKET nTcpSocket);
	~ClientConnection();

	u_short GetLastLatency() const;
	void SetLastLatency(u_short nLastLatency);

	HashMatcher<PingData_t, double> & GetPingSentTimes();

	u_int GetPlayerID() const;
	void SetPlayer(CPlayer * pPlayer);
	bool HasPlayer() const;
	CPlayer * GetPlayer();

	u_char		cCurrentUpdateSequenceNumber;

	struct TcpPacketBuffer_t {
		u_char		cTcpPacketBuffer[2 * MAX_TCP_PACKET_SIZE - 1];	// Buffer for incoming TCP packets
		u_short		nCurrentPacketSize;

		TcpPacketBuffer_t() : nCurrentPacketSize(0) {}
	} oTcpPacketBuffer;

	static bool BroadcastTcp(CPacket & oPacket, JoinStatus nMinimumJoinStatus = IN_GAME);
	static bool BroadcastTcpExcept(CPacket & oPacket, ClientConnection * pConnection, JoinStatus nMinimumJoinStatus = IN_GAME);

	static bool BroadcastUdp(CPacket & oPacket, JoinStatus nMinimumJoinStatus = IN_GAME);
	static bool BroadcastUdpExcept(CPacket & oPacket, ClientConnection * pConnection, JoinStatus nMinimumJoinStatus = IN_GAME);

	static ClientConnection * GetFromTcpSocket(SOCKET nTcpSocket);
	static ClientConnection * GetFromUdpAddress(sockaddr_in & oUdpAddress);
	static ClientConnection * GetFromSignature(u_char cSignature[m_knSignatureSize]);

	static u_int size() { return m_oConnections.size(); }
	static void CloseAll();

private:
	u_short		m_nLastLatency;
	HashMatcher<PingData_t, double>		m_oPingSentTimes;

	CPlayer * m_pPlayer;

	static list<ClientConnection *>		m_oConnections;
};

#endif // __ClientConnection_H__
