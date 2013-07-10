class CPacket;

#define DEFAULT_PORT			9034

#define SIGNATURE_SIZE			8

// Packets
#define MAX_TCP_PACKET_SIZE		1448
#define MAX_UDP_PACKET_SIZE		1448
#define MAX_PACKET_SIZE			__max(MAX_TCP_PACKET_SIZE, MAX_UDP_PACKET_SIZE)

enum JoinStatus {
	DISCONNECTED = 0,
	TCP_CONNECTED,
	ACCEPTED,
	UDP_CONNECTED,
	IN_GAME
};

typedef struct {
	char	cMoveDirection;
	float	fZ;
	//char	cStealth;
} Input_t;

typedef struct {
	Input_t	oInput;
	State_t	oState;
} Move_t;

extern const float	kfInterpolate;
extern const float	kfMaxExtrapolate;

// Initialize the networking component
bool NetworkInit(void);

// Prints the last error code
void NetworkPrintError(const char *szMessage);

int sendall(SOCKET s, char *buf, int len, int flags);

// Connect to a server
bool NetworkConnect(char *szHost, int nPort);

bool NetworkCreateThread(void);

void GLFWCALL NetworkThread(void *pArgument);

// Process a received TCP packet
bool NetworkProcessTcpPacket(CPacket & oPacket/*, CClient * pClient*/);

// Process a received UDP packet
bool NetworkProcessUdpPacket(CPacket & oPacket, int nPacketSize/*, CClient * pClient*/);

void NetworkSendUdpHandshakePacket(void *pArgument);

void NetworkShutdownThread(void);

void NetworkDestroyThread(void);

// Shutdown the networking component
void NetworkDeinit(void);

// Closes a socket
void NetworkCloseSocket(SOCKET nSocket);