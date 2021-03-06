#include "GameSystem.h"
#include "GameNetwork.h"
#include "GameInterface.h"
#include "ResourcePath.h"
#include <algorithm>

using namespace Gameplay;

void Gameplay::GameSystem::reloadNPCRenderLlist()
{
	//find and load every NPC into the tree
	npcRenderList.clear();
	auto& layerVector = currentMap->GetLayers();
	auto NPCLayer = find_if(layerVector.begin(), layerVector.end(), [&](tmx::MapLayer& layer)
	{
		return layer.name == "NPC";
	});
	if (NPCLayer != layerVector.end())
	{
		for (tmx::MapObject& npc : NPCLayer->objects)
		{
			if (npc.GetType() == "NPC")
			{
				std::string name = npc.GetName();
				assert(name != "");
				std::string spriteFile = npc.GetPropertyString("source");
				std::unique_ptr<NPC> npcPtr(new NPC(config.fontMan.get("Carlito-Regular.ttf"), config.texMan.get(spriteFile), currentMap, name));
				npcPtr->setPosition(npc.GetPosition());

				std::stringstream ss;
				int x, y;
				ss.str(npc.GetPropertyString("upRect"));
				ss >> x >> y;
				sf::IntRect upRect(x, y, 32, 32);
				ss.clear();
				ss.str(npc.GetPropertyString("rightRect"));
				ss >> x >> y;
				sf::IntRect rightRect(x, y, 32, 32);
				ss.clear();
				ss.str(npc.GetPropertyString("downRect"));
				ss >> x >> y;
				sf::IntRect downRect(x, y, 32, 32);
				ss.clear();
				ss.str(npc.GetPropertyString("leftRect"));
				ss >> x >> y;
				sf::IntRect leftRect(x, y, 32, 32);
				npcPtr->setSpriteRect(upRect, rightRect, downRect, leftRect);
				std::string strBuff = npc.GetPropertyString("default_direction");
				if (strBuff == "up")
					npcPtr->setDirection(NPC::DIRECTION::up);
				else if (strBuff == "right")
					npcPtr->setDirection(NPC::DIRECTION::right);
				else if (strBuff == "left")
					npcPtr->setDirection(NPC::DIRECTION::left);
				else
					npcPtr->setDirection(NPC::DIRECTION::down);

				npcRenderList.push_back(std::move(npcPtr));
			}
		}
	}
}

GameSystem::GameSystem(Configuration& newConfig, std::unique_ptr<StartInfo>& startInfoPtr) :
	config(newConfig),
	battleFactory(newConfig),
	itemLoader(newConfig)
{
	//create the players
    for(StartInfo::Player& player : startInfoPtr->playerList)
    {
        playerTree.emplace(player.name, Player(config, player.name, player.name));
    }
    thisPlayerPtr = &playerTree.at(config.player_name);

	//load the map
	//TBD, load every map needed
	loadMap("test.tmx");
    loadMap("Test2.tmx");
	loadMap("town.tmx");
	loadMap("world.tmx");
	loadMap("dungeon_1.tmx");

	for (auto& pair : playerTree)
	{
		addPlayertoMap(pair.second.getName(), "town.tmx", "event_start");
	}
}

void Gameplay::GameSystem::movePlayer(const std::string& playerName, const Character::Direction & direction)
{
	//if the player is in battle, do the input for battle only
	Player& player = playerTree.at(playerName);
	if (player.getState() == Player::STATE::inBattle)
	{
		switch (direction)
		{
		case Character::Direction::left:
			currentBattle->moveCharacter(playerName, BattleCharacter::DIRECTION::left);
			break;
		case Character::Direction::right:
			currentBattle->moveCharacter(playerName, BattleCharacter::DIRECTION::right);
			break;
		}
	}
	else
	{
		tmx::MapObject* eventObject = player.moveCharacter(currentMap, direction);

		//if pointer points to a event object, handle event
		if (eventObject && playerName == thisPlayerPtr->getName())
		{
			handleGameEvent(eventObject);
		}
	}
}

void Gameplay::GameSystem::addPlayertoMap(const std::string& playerName, const std::string& mapName, const std::string& location)
{
	//mapName TBD
	//...
	//if the player is the one controling this computer...
	if (playerName == config.player_name)
	{
		currentMap = mapTree.at(mapName);
		Player& player = playerTree.at(playerName);
		player.changeMap(currentMap, location);
		reloadNPCRenderLlist();
		//load music
		config.musMan.stopAll();
		std::string musicName = currentMap->GetPropertyString("music");
		sf::Music& music = config.musMan.get(musicName);
		music.play();
	}
	else //if it is another player...
	{
		tmx::MapLoader* newMap = mapTree.at(mapName);
		Player& player = playerTree.at(playerName);
		player.changeMap(newMap, location);
	}
}

void Gameplay::GameSystem::handleGameEvent(tmx::MapObject* eventObject)
{
	if (eventObject->GetType() == "teleport")
	{
		std::string destination = eventObject->GetPropertyString("destination");
		std::string destination_point = eventObject->GetPropertyString("destination_point");
		interfacePtr->setTransition();
        addPlayertoMap(thisPlayerPtr->getName(), destination, destination_point);
		interfacePtr->exitTransition();

		sf::Packet packet;
		packet << "changeMap";
		packet << config.player_name;
		packet << destination << destination_point;

		//if this is client, send the changeMap signal to the server
		if (networkPtr->getServerIP() != sf::IpAddress::None)
		{
			networkPtr->send(networkPtr->getServerIP(), packet);
		}
		else //if this is server, boardcast the signal
		{
			networkPtr->boardCast(packet);
		}
	}
    else if(eventObject->GetType() == "dialogue")
    {
        interfacePtr->switchDialogue(eventObject->GetPropertyString("content"));
    }
	else if (eventObject->GetType() == "battle")
	{
		std::cout << "Battle encountered." << std::endl;
        std::cout << "Name : " << eventObject->GetName() << std::endl;
        createBattle(thisPlayerPtr->getName(), eventObject);
	}
	else if (eventObject->GetType() == "NPC")
	{
		std::string interaction = eventObject->GetPropertyString("interaction");
		if (interaction == "dialogue")
		{
            changeNPCDirection(eventObject->GetName());
            interfacePtr->switchDialogue(eventObject->GetPropertyString("content"));
		}
        else if(interaction == "heal")
        {
            int max_hp = thisPlayerPtr->getMaxHp();
            thisPlayerPtr->setCurrentHp(max_hp);
            changeNPCDirection(eventObject->GetName());
            interfacePtr->switchDialogue("This is free healing service");
        }
	}
}

void Gameplay::GameSystem::changeNPCDirection(const std::string& name)
{
    //find the npc and change his/her direction
    for(auto it = npcRenderList.begin(); it != npcRenderList.end(); it++)
    {
        if(name == (*it)->getName())
        {
            if(thisPlayerPtr->getDirection() == Character::Direction::left)
            {
                (*it)->setDirection(NPC::DIRECTION::right);
            }
            else if(thisPlayerPtr->getDirection() == Character::Direction::right)
            {
                (*it)->setDirection(NPC::DIRECTION::left);
            }
            else if(thisPlayerPtr->getDirection() == Character::Direction::up)
            {
                (*it)->setDirection(NPC::DIRECTION::down);
            }
            else if(thisPlayerPtr->getDirection() == Character::Direction::down)
            {
                (*it)->setDirection(NPC::DIRECTION::up);
            }
            break;
        }
    }
}

void Gameplay::GameSystem::loadMap(const std::string & filename)
{
	tmx::MapLoader* newMap = new tmx::MapLoader(resourcePath() + "maps");

	if (!newMap->Load(filename))
	{
		throw "Map " + filename + " not found!";
	}

	//place every player in the corner of the map
	//TBD, only testing map available
	auto& layerVector = newMap->GetLayers();

	//reserve the vector to prevent reallocation
	//layerVector.reserve(8);

	auto playerLayer = find_if(layerVector.begin(), layerVector.end(), [&](tmx::MapLayer& layer)
	{
		return layer.name == "Player";
	});

	if (playerLayer == layerVector.end())
		throw "not found!";

	//reserve the vector to prevent reallocation
	//playerLayer->objects.reserve(8);

	//create a map object representing player
    for(auto& pair: playerTree)
    {
        Player& player = pair.second;
        tmx::MapObject playerObj;
        playerObj.SetShapeType(tmx::MapObjectShape::Rectangle);
        playerObj.SetSize(sf::Vector2f(20, 12));
        playerObj.SetPosition(-50, -50);
        playerObj.AddPoint(sf::Vector2f());
        playerObj.AddPoint(sf::Vector2f(20, 0));
        playerObj.AddPoint(sf::Vector2f(20, 12));
        playerObj.AddPoint(sf::Vector2f(0, 12));
        playerObj.CreateDebugShape(sf::Color::Blue);
        playerObj.SetName(player.getName());
        
        playerLayer->objects.push_back(std::move(playerObj));
    }

	//push the map into the tree
	mapTree.emplace(filename, std::move(newMap));
}

void Gameplay::GameSystem::setReady(const std::string& playerName, const bool& newState)
{
	//get the target player
	Player& player = playerTree.at(playerName);
	player.setReady(newState);
}

void Gameplay::GameSystem::setPlayerPosition(const std::string & playerName, sf::Vector2f pos)
{
	Player& player = playerTree.at(playerName);
	player.setCharacterPosition(pos);
}

void Gameplay::GameSystem::interact()
{
    //ask player to "interect".
    tmx::MapObject* eventObj = thisPlayerPtr->interact();
    
    //if eventObj is not null, handle event.
    if(eventObj)
    {
        handleGameEvent(eventObj);
    }
}

void Gameplay::GameSystem::updateQuadTree()
{
	const sf::View& camera = interfacePtr->getCamera();
	sf::Vector2f position = camera.getCenter() - sf::Vector2f(camera.getSize().x / 2, camera.getSize().y / 2);
	sf::FloatRect cameraRect(position, camera.getSize());
	currentMap->UpdateQuadTree(cameraRect);
}

void Gameplay::GameSystem::createBattle( const std::string& initPlayerName, tmx::MapObject* battleObj)
{
    interfacePtr->setTransition();
    //get the Player from name
    Player& initPlayer = playerTree.at(initPlayerName);
    //get the position of player
    sf::Vector2f playerPos = initPlayer.getPosition();
    //create battle
    std::shared_ptr<Battle> battle(battleFactory.generateBattle(battleObj));
    //the player joins the battle
    initPlayer.joinBattle(battle);
	if (initPlayerName == thisPlayerPtr->getName())
	{
		currentBattle = battle;
		interfacePtr->hideMiniMap();
	}       
    interfacePtr->exitTransition();
}

void Gameplay::GameSystem::deleteBattle()
{
    interfacePtr->setTransition();
	thisPlayerPtr->leaveBattle();
    currentBattle.reset();
	//if the player is defeated, teleport to the last safeLocation
	if (thisPlayerPtr->getCurrentHp() <= 0)
	{
		thisPlayerPtr->teleport_ToLastSafeLocation();
		currentMap = thisPlayerPtr->getMap();
		reloadNPCRenderLlist();
		thisPlayerPtr->setCurrentHp(1);
	}	
	interfacePtr->showMiniMap();
    interfacePtr->exitTransition();
}

bool Gameplay::GameSystem::isBattleOver()
{
    if(currentBattle)
    {
        return currentBattle->getState() == Battle::STATE::overed;
    }
    return true;    //there is no battle, return true anyway...
}
