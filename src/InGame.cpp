#include "InGame.h"
#include <sstream>

InGame::InGame(Configuration& newConfig, std::unique_ptr<StartInfo> startInfo) :
	config(newConfig)
{
	loadGame(startInfo);
}

InGame::~InGame()
{
	delete interfacePtr;
	delete networkPtr;
	delete systemPtr;
}

void InGame::run()
{
	sf::RenderWindow& window = config.window;
	window.setFramerateLimit(60);
	
	while (window.isOpen())
	{
		//input & update phase
		systemPtr->updateQuadTree();
		networkPtr->update();
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();

            if(event.type == sf::Event::KeyPressed)
            {
                if(sf::Keyboard::isKeyPressed(sf::Keyboard::Space))
                    systemPtr->interact();
            }
            
			interfacePtr->updateGUI(event);
		}

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left))
        {
            systemPtr->movePlayer(config.player_name, Gameplay::Character::Direction::left);
            
            if (networkPtr->getServerIP() != sf::IpAddress::None)
            {
                sf::Packet packet;
                packet << "move";
                packet << "left";
                networkPtr->send(networkPtr->getServerIP(), packet);
            }
        }
        else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right))
        {
            systemPtr->movePlayer(config.player_name, Gameplay::Character::Direction::right);
            if (networkPtr->getServerIP() != sf::IpAddress::None)
            {
                sf::Packet packet;
                packet << "move";
                packet << "right";
                networkPtr->send(networkPtr->getServerIP(), packet);
            }
        }
        else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down))
        {
            systemPtr->movePlayer(config.player_name, Gameplay::Character::Direction::down);
            if (networkPtr->getServerIP() != sf::IpAddress::None)
            {
                sf::Packet packet;
                packet << "move";
                packet << "down";
                networkPtr->send(networkPtr->getServerIP(), packet);
            }
        }
        else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up))
        {
            systemPtr->movePlayer(config.player_name, Gameplay::Character::Direction::up);
            if (networkPtr->getServerIP() != sf::IpAddress::None)
            {
                sf::Packet packet;
                packet << "move";
                packet << "up";
                networkPtr->send(networkPtr->getServerIP(), packet);
            }
        }

		config.cursor.update();

		//rendering phase
		window.clear();
		interfacePtr->draw();
		//window.draw(config.cursor);
		window.display();
	}
}

void InGame::loadGame(std::unique_ptr<StartInfo>& startInfo)
{
	//graphics in loading screen
	sf::RenderWindow& window = config.window;
	tgui::Gui gui;
	gui.setWindow(window);

	gui.setFont(tgui::Font(config.fontMan.get("Carlito-Bold.ttf")));

	tgui::Picture::Ptr background = std::make_shared<tgui::Picture>();
	background->setTexture(config.texMan.get("Book.png"));
	background->setSize(gui.getSize());
	gui.add(background);

	tgui::ProgressBar::Ptr progressBar = std::make_shared<tgui::ProgressBar>();
	gui.add(progressBar);
	progressBar->setPosition(50, 700);
	progressBar->setSize(930, 20);
	progressBar->setMinimum(0);
	progressBar->setMaximum(100);
	progressBar->setText("loading...0%");
	unsigned int percent = 0;

	tgui::Panel::Ptr panel = std::make_shared<tgui::Panel>();
	gui.add(panel);
	panel->setSize(820, 200);
	panel->setPosition(100, 450);
	panel->setBackgroundColor(tgui::Color(192, 192, 192, 150));

	tgui::Label::Ptr tips = std::make_shared<tgui::Label>();
	panel->add(tips);
	tips->setPosition(20, 20);
	tips->setTextSize(24);
	tips->setText("This is testing. Click cross button to leave.");
	
	//*************************************************************************
	//the render loop
	bool loading_complete = false;	//leave the loop when loading completed
	Connection* temp_networkPtr = new Connection;
	if (!temp_networkPtr)
	{
		throw "Failed to create network module!";
	}

	while (window.isOpen() && !loading_complete)
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();

			gui.handleEvent(event);
		}

		config.cursor.update();

		//if still loading, update percent
		if (percent < 99)
		{
			if (clock.getElapsedTime() > sf::seconds(0.05))
			{
				percent++;
				std::stringstream ss;
				ss << percent;
				progressBar->setText(sf::String("loading...") + sf::String(ss.str()) + sf::String("%"));
				progressBar->setValue(percent);
				clock.restart();
			}
		}
		else	//else, client: send "ready" signal to server for every 5s; server: start anyway
		{
			progressBar->setText(sf::String("waiting for server..."));
			loading_complete = waitForStart(startInfo, temp_networkPtr);
		}

		window.clear();
		gui.draw();
		window.draw(config.cursor);
		window.display();
	}
	delete temp_networkPtr;
	//*************************************************************************
	//if it is server, start server system...TBD
	systemPtr = new Gameplay::GameSystem(config, startInfo);
	if (startInfo->type == StartInfo::TYPE::Server)
	{
		//systemPtr = new Gameplay::ServerSystem(config);
	}
	else //else it is client, start client system
	{
		//systemPtr = new Gameplay::ClientSystem();
	}

	//create network and interface which is pointing to the game system
	networkPtr = new Gameplay::GameNetwork(systemPtr, startInfo);

	systemPtr->setNetworkPtr(networkPtr);

	interfacePtr = new Gameplay::GameInterface(systemPtr);

	systemPtr->setInterfacePtr(interfacePtr);

	if (startInfo->type == StartInfo::TYPE::Server)
	{
		//send ready signal to every player
		//...
	}
	else //if it is a client
	{
		//wait for server's signal
	}
}

bool InGame::waitForStart(std::unique_ptr<StartInfo>& startInfoPtr, Connection * connectionPtr)
{
	if (startInfoPtr->type == StartInfo::TYPE::Server)
	{
		return true;
	}
	else
	{
		//if recevied somthing, if that's from server and the signal if "start", return true
		if (!connectionPtr->empty())
		{
			Package& package = connectionPtr->front();
			std::string signal;
			package.packet >> signal;
			if (package.ip == startInfoPtr->serverIP && signal == "start")
			{
				connectionPtr->pop();
				return true;
			}
		}
		else //else, send "ready" signal to server for every 2s
		{
			if (clock.getElapsedTime() > sf::seconds(2))
			{
				sf::Packet packet;
				packet << "ready";
				connectionPtr->send(startInfoPtr->serverIP, packet);
			}
		}
	}
	return false;
}

