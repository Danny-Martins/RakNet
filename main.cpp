#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
#include "BitStream.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>
#include <string>

void send_player_stats();
void get_next_turn();
void check_player_death();
void check_winner();
void OnPlayerWin(RakNet::Packet* packet);

static unsigned int SERVER_PORT = 65000;
static unsigned int CLIENT_PORT = 65001;
static unsigned int MAX_CONNECTIONS = 3;

enum NetworkState
{
	NS_Init = 0,
	NS_PendingStart = 1,
	NS_Started = 2,
	NS_Lobby = 3,
	NS_Pending = 4,
	NS_Game_Playing = 5,
	NS_Game_Pending = 6,
	NS_Game_Server_Pending = 7,
	NS_Game_over = 8,
};

bool isServer = false;
bool isRunning = true;

RakNet::RakPeerInterface *g_rakPeerInterface = nullptr;
RakNet::SystemAddress g_serverAddress;

std::mutex g_networkState_mutex;
NetworkState g_networkState = NS_Init;

RakNet::RakString local_player_name = "NAME ERROR";
unsigned int player_turn = 0;
std::string* player_array[3];

enum {
ID_THEGAME_LOBBY_READY = ID_USER_PACKET_ENUM,
ID_PLAYER_READY,
ID_THEGAME_START, 
ID_CHARACTER_SELECT,
ID_GAME_STARTING,
ID_CLIENT_HEAL,
ID_CLIENT_ATTACK,
ID_SET_CLIENT_TURN,
ID_REQUEST_PLAYER_STATS,
ID_PLAYER_STATS,
ID_PLAYER_DEATH,
ID_WINNER,
};

enum EPlayerClass
{
	Mage = 0,
	Rogue = 1,
	Cleric = 2,
	Error = 3,
};

std::string class_to_name(EPlayerClass clas) {
	if (clas == EPlayerClass::Mage)
		return "Mage";
	if (clas == EPlayerClass::Rogue)
		return "Rouge";
	if (clas == EPlayerClass::Cleric)
		return "Cleric";
	return "ERROR IN CLASS TO NAME FUNCTION";
}

struct SPlayer
{
	std::string m_name;
	int m_health = 5;
	EPlayerClass m_class = Error;

	//function to send a packet with name/health/class etc
	void SendName(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(m_name.c_str());
		writeBs.Write(name);

		RakNet::BitStream class_bitstream;
		class_bitstream.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString clas(class_to_name(m_class).c_str());
		class_bitstream.Write(clas);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
		//assert(g_rakPeerInterface->Send(&class_bitstream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}
};

std::map<unsigned long, SPlayer> m_players;

SPlayer& GetPlayer(RakNet::RakNetGUID raknetId)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(raknetId);
	std::map<unsigned long, SPlayer>::iterator it = m_players.find(guid);
	assert(it != m_players.end());
	return it->second;
}

void OnLostConnection(RakNet::Packet* packet)
{
	SPlayer& lostPlayer = GetPlayer(packet->guid);
	lostPlayer.SendName(RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
	unsigned long keyVal = RakNet::RakNetGUID::ToUint32(packet->guid);
	m_players.erase(keyVal);
}

//server
void OnIncomingConnection(RakNet::Packet* packet)
{
	//must be server in order to recieve connection
	assert(isServer);
	m_players.insert(std::make_pair(RakNet::RakNetGUID::ToUint32(packet->guid), SPlayer()));
	std::cout << "Total Players: " << m_players.size() << std::endl;
}

//client
void OnConnectionAccepted(RakNet::Packet* packet)
{
	//server should not ne connecting to anybody, 
	//clients connect to server
	assert(!isServer);
	g_networkState_mutex.lock();
	g_networkState = NS_Lobby;
	g_networkState_mutex.unlock();
	g_serverAddress = packet->systemAddress;
}

//this is on the client side
void DisplayPlayerReady(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	std::cout << userName.C_String() << " has joined" << std::endl;
}

void OnPlayerSelectedCharacter(RakNet::Packet* packet) {
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString character;
	bs.Read(character);

	//std::cout << character << std::endl;

	EPlayerClass temp_class = EPlayerClass::Mage;

	if (character == "Mage") {
		//std::cout << "chose mage" << std::endl;
		//temp_class == EPlayerClass::Mage;
	}
	else  if (character == "Rouge") {
		//std::cout << "chose Rouge" << std::endl;
		temp_class = EPlayerClass::Rogue;
	}
	else if (character == "Cleric") {
		//std::cout << "chose Cleric" << std::endl;
		temp_class = EPlayerClass::Cleric;
	}
	else
		std::cout << "error no class chosen or player cant type, defaulting to Mage" << std::endl;
		

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);
	player.m_class = temp_class;

	std::cout << player.m_name.c_str() << " Chose " << class_to_name(player.m_class).c_str() << " and is ready for battle! " << std::endl;
	player.SendName(packet->systemAddress, true);

	if (m_players.size() == MAX_CONNECTIONS) {
		std::cout << std::endl << "ALL PLAYERS CONNECTED" << std::endl;
		for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it) {
			if (it->second.m_class == EPlayerClass::Error) {
				std::cout << "found error meaning not all players are ready... returning" << std::endl;
				return;
			}
			std::cout << it->second.m_name.c_str() << " : " << class_to_name(player.m_class).c_str() << std::endl;
		}

		//std::cout << "Starting "
		system("cls");
		std::cout << "Starting Main Game \n " << std::endl;
		
		g_networkState_mutex.lock();
		g_networkState = NS_Game_Server_Pending;
		g_networkState_mutex.unlock();

		std::cout << g_networkState;

		int i = 0;
		for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it) {
			player_array[i] = &it->second.m_name;
			i++;
		}

		/*
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_GAME_STARTING);
		RakNet::RakString player_name(player_array[player_turn]->c_str());
		writeBs.Write(player_name);
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
		*/

		get_next_turn();
		send_player_stats();
	}
}

//server only
void get_next_turn() {
	player_turn++;

	if (player_turn >= MAX_CONNECTIONS)
		player_turn = 0;

	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it) {
		if (*player_array[player_turn] == it->second.m_name) {
			if (it->second.m_health <= 0)
				player_turn++;
		}
	}

	if (player_turn >= MAX_CONNECTIONS)
		player_turn = 0;

	RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_GAME_STARTING);
	RakNet::RakString player_name(player_array[player_turn]->c_str());
	writeBs.Write(player_name);
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
}

//server only
void send_player_stats() {
	RakNet::BitStream bit_stream;
	bit_stream.Write((RakNet::MessageID)ID_PLAYER_STATS);

	std::string temp_string;

	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it) {
		temp_string.append("Name: ");
		temp_string.append(it->second.m_name);
		temp_string.append("\n");
		temp_string.append("Class: ");
		temp_string.append(class_to_name(it->second.m_class));
		temp_string.append("\n");
		temp_string.append("Health Left: ");
		temp_string.append(std::to_string(it->second.m_health));
		temp_string.append("\n");
		temp_string.append("\n");
	}

	RakNet::RakString temp_rak_string(temp_string.c_str());
	bit_stream.Write(temp_rak_string);

	assert(g_rakPeerInterface->Send(&bit_stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));

}
//server only
void check_player_death(SPlayer player) {
	if (player.m_health <= 0) {
		RakNet::BitStream bit_stream;
		bit_stream.Write((RakNet::MessageID)ID_PLAYER_DEATH);
		RakNet::RakString name(player.m_name.c_str());
		bit_stream.Write(name);
		assert(g_rakPeerInterface->Send(&bit_stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
		check_winner();
	}
}
//server only
void check_winner() {
	int alive_players = 0;
	RakNet::RakString winning_player_name;
	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it) {
		if (it->second.m_health > 0) {
			winning_player_name = it->second.m_name.c_str();
			alive_players++;
		}
	}

	if (alive_players <= 1) {
		RakNet::BitStream bit_stream;
		bit_stream.Write((RakNet::MessageID)ID_WINNER);
		bit_stream.Write(winning_player_name);
		assert(g_rakPeerInterface->Send(&bit_stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
		//crash server because reasons
		g_networkState = NS_Game_over;
	}

}

//server only
void OnClientAttack(RakNet::Packet* packet) {
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString player_name;
	bs.Read(player_name);

	int damage_dealt = 2;
	if (GetPlayer(packet->guid).m_class == EPlayerClass::Rogue)
		damage_dealt = 3;

	std::cout << GetPlayer(packet->guid).m_name << " attacked " << player_name << " for " << damage_dealt << " damage!" << std::endl;

	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it) {
		RakNet::RakString temp_name = it->second.m_name.c_str();
		if (player_name == temp_name) {
			it->second.m_health -= damage_dealt;
			check_player_death(it->second);
			//break;
		}
		//std::cout << "in the loop" << std::endl;
	}
	
	//std::cout << "before get next turn and such" << std::endl;

	get_next_turn();
	send_player_stats();

	//std::cout << "end of function" << std::endl;

}

//server only
void OnClientHeal(RakNet::Packet* packet) {
	//m_players.find()
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	if(GetPlayer(packet->guid).m_class == EPlayerClass::Cleric)
		GetPlayer(packet->guid).m_health += 3;
	else
		GetPlayer(packet->guid).m_health += 2;
	
	std::cout << GetPlayer(packet->guid).m_name.c_str() << " HP: " << GetPlayer(packet->guid).m_health << std::endl;
	get_next_turn();
	send_player_stats();
}
//client only
void OnPlayerWin(RakNet::Packet* packet) {
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString winner_name;
	bs.Read(winner_name);

	system("cls");
	std::cout << "GAME OVER" << " The Winner is: " << winner_name << std::endl;
	std::cout << "Crashing game in 5 seconds" << std::endl;

	g_networkState = NS_Game_over;

}
//client only
void OnGameStarted(RakNet::Packet* packet) {
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	//system("cls");
	std::cout << "================================" << std::endl;
	std::cout << "your name is: " << local_player_name << std::endl;

	if (local_player_name == userName) {
		std::cout << "Its your turn!" << std::endl;
		g_networkState_mutex.lock();
		g_networkState = NS_Game_Playing;
		g_networkState_mutex.unlock();
	}
	else {
		std::cout << "Its " << userName << "'s turn" << std::endl;
		g_networkState_mutex.lock();
		g_networkState = NS_Game_Pending;
		g_networkState_mutex.unlock();
	}
	
	//std::cout << g_networkState << std::endl;
}

void OnRecivePlayerDeath(RakNet::Packet* packet) {
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString dead_player_name;
	bs.Read(dead_player_name);

	std::cout << "================================" << std::endl;
	std::cout << dead_player_name << " has died rip " << std::endl;

}

//client only
void OnRecivePlayerStats(RakNet::Packet* packet) {
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString player_stats;
	bs.Read(player_stats);

	std::cout << std::endl << "Player Stats" << std::endl << player_stats;
}

void OnLobbyReady(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);
	player.m_name = userName;

	//notify all other connected players that this plyer has joined the game
	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		//skip over the player who just joined
		if (guid == it->first)
		{
			continue;
		}

		SPlayer& player = it->second;
		//player.SendName(packet->systemAddress, false);
		/*RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(player.m_name.c_str());
		writeBs.Write(name);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false));*/
	}
	
	//player.SendName(packet->systemAddress, true);
	/*RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
	RakNet::RakString name(player.m_name.c_str());
	writeBs.Write(name);
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, true));*/

}

unsigned char GetPacketIdentifier(RakNet::Packet *packet)
{
	if (packet == nullptr)
		return 255;

	if ((unsigned char)packet->data[0] == ID_TIMESTAMP)
	{
		RakAssert(packet->length > sizeof(RakNet::MessageID) + sizeof(RakNet::Time));
		return (unsigned char)packet->data[sizeof(RakNet::MessageID) + sizeof(RakNet::Time)];
	}
	else
		return (unsigned char)packet->data[0];
}


void InputHandler()
{
	while (isRunning)
	{
		char userInput[255];
		if (g_networkState == NS_Init)
		{
			std::cout << "press (s) for server (c) for client" << std::endl;
			std::cin >> userInput;
			isServer = (userInput[0] == 's');
			g_networkState_mutex.lock();
			g_networkState = NS_PendingStart;
			g_networkState_mutex.unlock();
		}
		else if (g_networkState == NS_Lobby)
		{
			std::cout << "Enter your name to play or type quit to leave" << std::endl;
			std::cin >> userInput;
			//quitting is not acceptable in our game, create a crash to teach lesson
			assert(strcmp(userInput, "quit"));

			local_player_name = userInput;

			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_THEGAME_LOBBY_READY);
			RakNet::RakString name(userInput);
			bs.Write(name);

			std::cout << "Select Your Class: " << std::endl << "1: Mage (Has Pointy Hat)" << std::endl
				<< "2: Rouge (Extra Damage)" << std::endl << "3: Cleric (Extra Heal)" << std::endl;
			std::cin >> userInput;

			RakNet::BitStream bit_stream;
			bit_stream.Write((RakNet::MessageID)ID_CHARACTER_SELECT);
			RakNet::RakString clas(userInput);
			bit_stream.Write(clas);

			//returns 0 when something is wrong
			assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			assert(g_rakPeerInterface->Send(&bit_stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			g_networkState_mutex.lock();
			g_networkState = NS_Pending;
			g_networkState_mutex.unlock();
		}
		else if (g_networkState == NS_Pending)
		{
			static bool doOnce = false;
			if (!doOnce) {
				std::cout << "Waiting for server to start" << std::endl;
			}

			doOnce = true;
		}
		else if (g_networkState == NS_Game_Playing) {
			// wait thread while player stats are being sent
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			std::cout << "Make a move: Attack, Heal" << std::endl;
			std::string input;
			std::cin >> input;

			if (input == "Heal") {
				RakNet::BitStream bit_stream;
				bit_stream.Write((RakNet::MessageID)ID_CLIENT_HEAL);
				assert(g_rakPeerInterface->Send(&bit_stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
				g_networkState = NS_Game_Pending;
			}
			else if (input == "Attack") {
				std::cout << "Type the name of your attack target" << std::endl;
				input.clear();
				std::cin >> input;

				RakNet::RakString player_name(input.c_str());
				RakNet::BitStream bit_stream;
				bit_stream.Write((RakNet::MessageID)ID_CLIENT_ATTACK);
				bit_stream.Write(player_name);

				assert(g_rakPeerInterface->Send(&bit_stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
				g_networkState = NS_Game_Pending;
			}
			else {
				std::cout << "unknown input: " << userInput << std::endl;
			}

			
		}
		else if (g_networkState == NS_Game_over) {
			std::this_thread::sleep_for(std::chrono::seconds(5));
			assert(false);
		}
		std::this_thread::sleep_for(std::chrono::microseconds(50000));
	}	
}

bool HandleLowLevelPackets(RakNet::Packet* packet)
{
	bool isHandled = true;
		// We got a packet, get the identifier with our handy function
		unsigned char packetIdentifier = GetPacketIdentifier(packet);

		// Check if this is a network message packet
		switch (packetIdentifier)
		{
		case ID_DISCONNECTION_NOTIFICATION:
			// Connection lost normally
			printf("ID_DISCONNECTION_NOTIFICATION\n");
			break;
		case ID_ALREADY_CONNECTED:
			// Connection lost normally
			printf("ID_ALREADY_CONNECTED with guid %" PRINTF_64_BIT_MODIFIER "u\n", packet->guid);
			break;
		case ID_INCOMPATIBLE_PROTOCOL_VERSION:
			printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
			break;
		case ID_REMOTE_DISCONNECTION_NOTIFICATION: // Server telling the clients of another client disconnecting gracefully.  You can manually broadcast this in a peer to peer enviroment if you want.
			printf("ID_REMOTE_DISCONNECTION_NOTIFICATION\n");
			break;
		case ID_REMOTE_CONNECTION_LOST: // Server telling the clients of another client disconnecting forcefully.  You can manually broadcast this in a peer to peer enviroment if you want.
			printf("ID_REMOTE_CONNECTION_LOST\n");
			break;
		case ID_NEW_INCOMING_CONNECTION:
			//client connecting to server
			OnIncomingConnection(packet);
			printf("ID_NEW_INCOMING_CONNECTION\n");
			break;
		case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
			OnIncomingConnection(packet);
			printf("ID_REMOTE_NEW_INCOMING_CONNECTION\n");
			break;
		case ID_CONNECTION_BANNED: // Banned from this server
			printf("We are banned from this server.\n");
			break;
		case ID_CONNECTION_ATTEMPT_FAILED:
			printf("Connection attempt failed\n");
			break;
		case ID_NO_FREE_INCOMING_CONNECTIONS:
			// Sorry, the server is full.  I don't do anything here but
			// A real app should tell the user
			printf("ID_NO_FREE_INCOMING_CONNECTIONS\n");
			break;

		case ID_INVALID_PASSWORD:
			printf("ID_INVALID_PASSWORD\n");
			break;

		case ID_CONNECTION_LOST:
			// Couldn't deliver a reliable packet - i.e. the other system was abnormally
			// terminated
			printf("ID_CONNECTION_LOST\n");
			OnLostConnection(packet);
			break;

		case ID_CONNECTION_REQUEST_ACCEPTED:
			// This tells the client they have connected
			printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n", packet->systemAddress.ToString(true), packet->guid.ToString());
			printf("My external address is %s\n", g_rakPeerInterface->GetExternalID(packet->systemAddress).ToString(true));
			OnConnectionAccepted(packet);
			break;
		case ID_CONNECTED_PING:
		case ID_UNCONNECTED_PING:
			printf("Ping from %s\n", packet->systemAddress.ToString(true));
			break;
		default:
			isHandled = false;
			break;
		}
		return isHandled;
}

void PacketHandler()
{
	while (isRunning)
	{
		for (RakNet::Packet* packet = g_rakPeerInterface->Receive(); packet != nullptr; g_rakPeerInterface->DeallocatePacket(packet), packet = g_rakPeerInterface->Receive())
		{
			if (!HandleLowLevelPackets(packet))
			{
				//our game specific packets
				unsigned char packetIdentifier = GetPacketIdentifier(packet);
				switch (packetIdentifier)
				{
				case ID_THEGAME_LOBBY_READY:
					OnLobbyReady(packet);
					break;
				case ID_PLAYER_READY:
					DisplayPlayerReady(packet);
					break;
				case ID_CHARACTER_SELECT:
					OnPlayerSelectedCharacter(packet);
					break;
				case ID_GAME_STARTING:
					OnGameStarted(packet);
					break;
				case ID_CLIENT_HEAL:
					OnClientHeal(packet);
					break;
				case ID_PLAYER_STATS:
					OnRecivePlayerStats(packet);
					break;
				case ID_CLIENT_ATTACK:
					OnClientAttack(packet);
					break;
				case ID_PLAYER_DEATH:
					OnRecivePlayerDeath(packet);
					break;
				case ID_WINNER:
					OnPlayerWin(packet);
					break;
				default:
					break;
				}
			}
		}
		
		std::this_thread::sleep_for(std::chrono::microseconds(50000));
	}
}

int main()
{
	/*
	RakNet::BitStream bit_stream;
	//RakNet::RakString derp();

	std::string derp;
	derp.append("Name: ");
	derp.append("flamie");

	RakNet::RakString herp(derp.c_str());

	bit_stream.Write(herp);

	RakNet::RakString plz_work;
	bit_stream.Read(plz_work);

	std::cout << plz_work;

	system("pause");
	*/
	g_rakPeerInterface = RakNet::RakPeerInterface::GetInstance();

	std::thread inputHandler(InputHandler);
	std::thread packetHandler(PacketHandler);

	while (isRunning)
	{
		if (g_networkState == NS_PendingStart)
		{
			if (isServer)
			{
				RakNet::SocketDescriptor socketDescriptors[1];
				socketDescriptors[0].port = SERVER_PORT;
				socketDescriptors[0].socketFamily = AF_INET; // Test out IPV4

				bool isSuccess = g_rakPeerInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
				assert(isSuccess);
				//ensures we are server
				g_rakPeerInterface->SetMaximumIncomingConnections(MAX_CONNECTIONS);
				std::cout << "server started" << std::endl;
				g_networkState_mutex.lock();
				g_networkState = NS_Started;
				g_networkState_mutex.unlock();
			}
			//client
			else
			{
				RakNet::SocketDescriptor socketDescriptor(CLIENT_PORT, 0);
				socketDescriptor.socketFamily = AF_INET;

				while (RakNet::IRNS2_Berkley::IsPortInUse(socketDescriptor.port, socketDescriptor.hostAddress, socketDescriptor.socketFamily, SOCK_DGRAM) == true)
					socketDescriptor.port++;

				RakNet::StartupResult result = g_rakPeerInterface->Startup(8, &socketDescriptor, 1);
				assert(result == RakNet::RAKNET_STARTED);
				
				g_networkState_mutex.lock();
				g_networkState = NS_Started;
				g_networkState_mutex.unlock();

				g_rakPeerInterface->SetOccasionalPing(true);
				//"127.0.0.1" = local host = your machines address
				RakNet::ConnectionAttemptResult car = g_rakPeerInterface->Connect("127.0.0.1", SERVER_PORT, nullptr, 0);
				RakAssert(car == RakNet::CONNECTION_ATTEMPT_STARTED);
				std::cout << "client attempted connection..." << std::endl;
				
			}
		}
		
	}

	inputHandler.join();
	packetHandler.join();

	return 0;
}
