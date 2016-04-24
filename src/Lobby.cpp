#include "Lobby.h"
/*
constructor for server's version
*/
Lobby::Lobby(Configuration & newConfig) :
	config(newConfig),
	type(Lobby::TYPE::server)
{
	initialize();
    
    //add server host into the lobby
    std::unique_ptr<lobby::Player> you(new lobby::Player(config.player_name, sf::IpAddress(), lobby::Character::SilverGuy));
	addPlayer(std::move(you));
}

/*
constructor for client's version
*/
Lobby::Lobby(Configuration& newConfig, sf::IpAddress newServerIP, sf::Packet& updatePacket) :
	config(newConfig),
	type(Lobby::TYPE::client),
	serverIP(newServerIP)
{
	initialize();
	
	playerList.clear();
	sf::Int8 size;
	updatePacket >> size;
	for (int i = 0; i < size; i++)
	{
		std::string name;
		int charName;
		updatePacket >> name;
		updatePacket >> charName;
		std::unique_ptr<lobby::Player> newPlayer(new lobby::Player(name, sf::IpAddress(), static_cast<lobby::Character::Name>(charName)));
		addPlayer(std::move(newPlayer));
	}
	updatePlayerList();
}
/*
Destructor for lobby
Client: notify the server that this player is leaving
Server: notigy every clinet that this server is closed
*/
Lobby::~Lobby()
{
	//send leaving signal here
	if (type == TYPE::client)
	{
		sf::Packet packet;
		packet << "lobby_leave";
		connection.send(serverIP, packet);
	}
	else
	{
		sf::Packet packet;
		packet << "lobby_serverClosed";
		for (auto it = playerList.begin(); it != playerList.end(); it++)
		{
			sf::IpAddress ip;
			ip = (*it)->getIP();
			connection.send(ip, packet);
		}
	}
}
void Lobby::initialize()
{
	panel = std::make_shared<tgui::Panel>();
	panel->setSize(822, 614);
	panel->setPosition(102, 77);
	panel->setBackgroundColor(tgui::Color(0, 0, 0, 60));

	backButton = std::make_shared<tgui::Button>();
	panel->add(backButton);
	backButton->setText("Back");
	backButton->setPosition(65, 542);

	startButton = std::make_shared<tgui::Button>();
	panel->add(startButton);
	startButton->setText("Start");
	startButton->setPosition(571, 546);
	startButton->connect("mousereleased", [&]() {

	});

	chatBox = std::make_shared<tgui::ChatBox>();
	panel->add(chatBox);
	chatBox->setSize(340, 150);
	chatBox->setPosition(51, 323);
	chatBox->addLine("Test");

	chatInput = std::make_shared<tgui::TextBox>();
	panel->add(chatInput);
	chatInput->setSize(340, 22);
	chatInput->setPosition(51, 473);
	chatInput->setMaximumCharacters(33);

	chatInputButton = std::make_shared<tgui::Button>();
	panel->add(chatInputButton);
	chatInputButton->setSize(34, 22);
	chatInputButton->setPosition(357, 473);
	chatInputButton->setText("send");
	chatInputButton->connect("mousereleased", [&]() {
		std::string str = chatInput->getText();
		if (str != "")
		{
			chatBox->addLine(str);
			chatInput->setText("");
		}

	});

	mapPicture = std::make_shared<tgui::Picture>();
	panel->add(mapPicture);
	mapPicture->setSize(234, 210);
	mapPicture->setPosition(535, 50);
	mapPicture->setTexture(config.texMan.get("shadow_kingdom_map.png"));
    
    playerListPanel = std::make_shared<tgui::Panel>();
    panel->add(playerListPanel);
    playerListPanel->setPosition(40,40);
    playerListPanel->setSize(400,250);
    playerListPanel->setBackgroundColor(tgui::Color(0,0,0,0));  //the panel is transparent
    
    playerListPanel_scrollBar = std::make_shared<tgui::Scrollbar>();
    playerListPanel_scrollBar->setSize(10,250);
    playerListPanel_scrollBar->setPosition(390, 0);
    playerListPanel_scrollBar->connect("valueChanged", [&](){
		updatePlayerList();
    });
}

void Lobby::addTgui(tgui::Gui & gui)
{
	gui.add(panel);
}

void Lobby::hide()
{
	panel->hide();
}

void Lobby::show()
{
	panel->show();
}

void Lobby::showWithEffect(const tgui::ShowAnimationType& effect, const sf::Time& time)
{
	panel->showWithEffect(effect, time);
}

void Lobby::update()
{
	if (!connection.empty())	//if the queue is not empty
	{
		Package package;
		package = connection.front();
		handlePacket(package);
		connection.pop();
	}
}

bool Lobby::addPlayer(std::unique_ptr<lobby::Player> playerPtr)
{
	if (playerList.size() >= MAX_PLAYER)
	{
		return false;
	}
	playerList.push_back(std::move(playerPtr));
	updatePlayerList();
	return true;
}

std::unique_ptr<StartInfo> Lobby::getStartInfo()
{
	std::unique_ptr<StartInfo> info(new StartInfo);
	//do something in here
	return info;
}

void Lobby::handlePacket(Package& package)
{
	std::string signal;
	package.packet >> signal;
	//for server
	if (type == TYPE::server)
	{
		/*
		Server : handles new player join in
		allocate a new player, call addPlayer(), and boardcast the update. 
		*/
		if (signal == "lobby_join")
		{
			std::string name;
			package.packet >> name;
			std::unique_ptr<lobby::Player> newPlayer(new lobby::Player(name, package.ip, lobby::Character::SilverGuy));
			if (addPlayer(std::move(newPlayer)))
			{
				std::cout << "new connection from " << package.ip << std::endl;

				//boardcast update here
				//1.create update packet
				sf::Packet update;
				update << "lobby_update";		//marked it as update packet
				sf::Int8 size = playerList.size();
				update << size;	//insert the size
												//2.insert each player into the packet
				for (auto& playerPtr : playerList)
				{
					update << *playerPtr;
				}
				//3.send the packet to each player
				for (auto& playerPtr : playerList)
				{
					connection.send(playerPtr->getIP(), update);
				}
			}
		}
		/*
		Server : handles player leaving server
		*/
		else if (signal == "lobby_leave")
		{
			for (auto& playerPtr : playerList)
			{
				if (package.ip == playerPtr->getIP())
				{
					playerList.remove(playerPtr);
					updatePlayerList();
					return;
				}
			}
		}
	}
	else	//for client
	{
		/*
		Client : server is closed.
		*/
		if (signal == "lobby_serverClosed")
		{
			//TBD, show server shut down message
		}
		/*
		Client : received update from server
		*/
		else if (signal == "lobby_update")
		{
			//clear the playerList
			playerList.clear();
			//get the size of the new playerList
			int size;
			package.packet >> size;
			for (int i = 0; i < size; i++)
			{
				std::string name;
				int charName;
				package.packet >> name;
				package.packet >> charName;
				std::unique_ptr<lobby::Player> newPlayer(new lobby::Player(name, sf::IpAddress(), static_cast<lobby::Character::Name>(charName)));
				addPlayer(std::move(newPlayer));
			}
			updatePlayerList();
		}
	}
}

void Lobby::updatePlayerList()
{
    playerListPanel->removeAllWidgets();

	//only show scrollBar if there are more than 5 players
	if (playerList.size() > 5)
	{
		playerListPanel->add(playerListPanel_scrollBar);
	}

	int i = 0;
	for (auto it = playerList.begin(); it != playerList.end(); it++)
	{
		auto ptr = (*it)->getPanel();
		ptr->setPosition(10, 10 + (i - playerListPanel_scrollBar->getValue()) * 50);
		playerListPanel->add(ptr);
		i++;
	}
}

lobby::Player::Player(std::string newName, sf::IpAddress newIP, Character newChar) :
	name(newName),
	ip(newIP),
	character(newChar)
{
	panel = std::make_shared<tgui::Panel>();
	panel->setSize(350, 40);

	nameText = std::make_shared<tgui::Label>();
	panel->add(nameText);
	nameText->setText(name);
	nameText->setPosition(80, 12);
	nameText->setTextColor(tgui::Color(0, 0, 0, 255));	//to be done - every player has different color

	charPic = std::make_shared<tgui::Picture>();
	panel->add(charPic);
	charPic->setSize(30, 30);
	charPic->setPosition(10, 5);
	character.setPic(charPic);
}

void lobby::Character::setPic(tgui::Picture::Ptr ptr)
{
	//create a temporary texture
	sf::Texture texture;
	//the filename to be access
	std::string filename = resourcePath() + "Texture/Actor4.png";

	switch (name)
	{
	case Name::SilverGuy :
		texture.loadFromFile(filename, sf::IntRect(0, 0, 32, 32));
		break;
	case Name::GoldGuy :
		texture.loadFromFile(filename, sf::IntRect(0, 128, 32, 32));
		break;
	case Name::RedGirl :
		texture.loadFromFile(filename, sf::IntRect(96, 0, 32, 32));
		break;
	case Name::BrownGirl :
		texture.loadFromFile(filename, sf::IntRect(96, 128, 32, 32));
		break;
	default:
		texture.loadFromFile(filename, sf::IntRect(0, 0, 32, 32));
	}
	ptr->setTexture(texture);
}
