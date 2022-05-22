#include "pch.h"
const int PORT = 307;
using namespace sf;
using json = nlohmann::json;


struct User
{
    sf::String nick;
    sf::TcpSocket* socket;
    int skin, //0 - cat, 1 - dog
    hp,// hp 0-100
    rank; 
    float weapon_rotation;
    bool right_direction;
    sf::Vector2f pos;
    int& token;
    std::string avatar;
    int id;
    std::vector<sf::Vector2f/*pos*/>bullets;
};

struct SessionData
{
    bool in_game = false, second_ready = false, error = false;
    std::vector<User> users;
    sf::Vector2f hp; //x - host, y - guest
    float finished = 0;
};

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

int apiLogin(std::string username, std::string password, sf::TcpSocket* socket)
{
    CURL* curl;
    CURLcode res;
    sf::Packet pak;
    std::string readBuffer = "";
    std::string url = "localhost:13378/api/game/login?username=" + username + "&password=" + password;
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    if (readBuffer.find("{") == std::string::npos)
    {
        readBuffer = "server error1";
        pak.clear();
        pak << 10 << sf::String(readBuffer);
        if (socket->send(pak) != sf::Socket::Done)
            std::cout << "nie mozna wyslac pakietu";
            return 0;
    }
        
    json j = json::parse(readBuffer);
    std::cout << '\n' << j["status"].get<std::string>();
    if (j["status"].get<std::string>() != "success")
    {
        readBuffer = "wrong login / password";
        pak.clear();
        std::cout << readBuffer;
        pak << 10 << sf::String(readBuffer);
        if (socket->send(pak) != sf::Socket::Done)
            std::cout << "nie mozna wyslac pakietu";
        return 0;
    } 

    readBuffer.clear();
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, std::string("localhost:13378/api/game/getuserdatabysecret?token=" +
            j["token"].get<std::string>()).c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    if (readBuffer == "" || readBuffer == "error")
    {
        readBuffer = "server error2";
        pak.clear();
        std::cout << readBuffer;
        pak << 10 << sf::String(readBuffer);
        if (socket->send(pak) != sf::Socket::Done)
            std::cout << "nie mozna wyslac pakietu";
        return 0;
    }

    j.clear();
    j = json::parse(readBuffer);
    if (j["status"].get<std::string>() != "success")
    {
        readBuffer = "server error3";
        pak.clear();
        std::cout << readBuffer;
        pak << 10 << sf::String(readBuffer);
        if (socket->send(pak) != sf::Socket::Done)
            std::cout << "nie mozna wyslac pakietu";
        return 0;
    }

    j = json::parse(readBuffer);
    sf::String name = j["data"]["display_name"].get<sf::String>(),
        desc = j["data"]["description"].get<sf::String>(),
        img = j["data"]["img"].get<sf::String>();
    int rank = j["data"]["rank"].get<int>(),
        money = j["data"]["money"].get<int>(),
        xp = j["data"]["xp"].get<int>(),
        points = j["data"]["points"].get<int>(),
        wins = j["data"]["wins"].get<int>(),
        losses = j["data"]["losses"].get<int>(),
        id = j["data"]["id"].get<int>();
    url = img;
    readBuffer = "";
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    pak.clear();
    pak << 0 << id << name << desc << rank << money << readBuffer << xp << points << wins << losses;
    if (socket->send(pak) != sf::Socket::Done)
        std::cout << "nie mozna wyslac pakietu";
    return 0;
}

void leaveGame(sf::TcpSocket* socket, int token, std::unordered_map<int /*token 6 digits*/, SessionData>& games)
{
    sf::Packet pak;
    if (games[token].in_game)//trwa gra
    {
        games[token].error = true;
    }
    else //lobby
    {
        if (games[token].users[0].socket == socket)//jesli wychodzi host
        {
            //wywal ludzi z lobby i usun lobby
            if (games[token].users.size() > 1)
            {
                pak.clear();
                pak << (int)1;
                if (games[token].users[1].socket->send(pak) != sf::Socket::Done)
                    std::cout << "NIE WYSLANO KOMUNIKATU O WYJSCIU HOSTA";
            }
            if(games[token].users.size()>1)
                games[token].users[1].token = 0;
            games.erase(token);
        }
        else//wychodzi guest
        {
            games[token].second_ready = false;
            games[token].users.pop_back();//usun guesta
            pak.clear();
            //wyslij info do hosta
            pak << (sf::Uint32)2 << "";
            if (games[token].users[0].socket->send(pak) != sf::Socket::Done)
                std::cout << "NIE WYSLANO DANYCH DO HOSTA";
        }
    }
}

int main()
{
    setlocale(LC_ALL, "");
    srand(time(NULL));
    std::cout << "type \"help\" to get command list\n";
    //Containers
    std::unordered_map<int /*token 6 digits*/, SessionData>games;
    std::vector<sf::FloatRect>obstacles{
        {335, 100, 130, 30},
        {335, 200, 130, 30},
        {205, 200, 130, 30},
        {465, 200, 130, 30},
        {335, 300, 130, 30},
        {335, 400, 130, 30},
        {335, 500, 130, 30},
        {0, 100, 130, 30},
        {670, 100, 130, 30},
        {0, 300, 130, 30},
        {670, 300, 130, 30},
        {102.5, 450, 130, 30},
        {567.5, 450, 130, 30}
    };
    //SOCKETS
    sf::TcpListener listener;
    std::vector<std::pair<sf::TcpSocket*, int /*game token, 0 - null*/>> clients;
    std::vector<std::unique_ptr<std::thread>>threads;
    sf::SocketSelector sel;
    if (listener.listen(PORT) != sf::Socket::Done)
    {
        std::cout << "Nie mogê rozpocz¹æ nas³uchiwania na porcie " << PORT << std::endl;
        exit(1);
    }
    else std::cout << "Slucham na porcie " << PORT << std::endl;
    sel.add(listener);

    std::thread cls([&]() {
        std::string command;
        while (true)
        {
            std::cin >> command;
            if (command == "cls")
                system("cls");
            else if (command == "clients")
                std::cout << std::endl << "NUMBER OF CLIENTS: " << clients.size() << std::endl;
            else if (command == "games")
                std::cout << std::endl << "NUMBER OF GAMES: " << games.size() << std::endl;
            else if (command == "port")
                std::cout << std::endl << "Listening on port " << PORT << std::endl;
            else if (command == "help")
                std::cout << "cls - clear cmd\nclients - number of clients\ngames - number of games\nport - info about connection\n";
        }
        });





    while (true)
    {
        if (sel.wait(sf::seconds(2)))
        {
            if (sel.isReady(listener))
            {
                sf::TcpSocket* tmp = new sf::TcpSocket;
                if (listener.accept(*tmp) == sf::TcpListener::Done)
                {
                    clients.push_back({ tmp,false });
                    sel.add(*tmp);
                    std::cout << "New connection.\n";
                }
                else
                {
                    delete tmp;
                }
            }

            for (auto it = clients.begin(); it != clients.end(); it++)
            {
                if (sel.isReady(*(*it).first)) // *clients[i] coœ nam wys³a³
                {
                    sf::Packet pak;
                    sf::TcpSocket::Status st;
                    st = (*it).first->receive(pak);
                    if (st == sf::Socket::Done)
                    {
                        int type;    
                        pak >> type;  
                        switch(type)
                        {
                        case 1://logowanie
                        {
                            std::cout << "Proba logowania" << std::endl;;
                            sf::String user, pass;
                            pak >> user >> pass;
                            threads.push_back(std::make_unique<std::thread>(apiLogin, user, pass, it->first));
                            threads.back()->detach();
                        }
                            break;
                        case 2://rejestracja
                            break;
                        case 3://create lobby
                        {
                            sf::String username;
                            int rank, id;
                            std::string avatar;
                            pak >> username >> rank >> avatar >> id;
                            pak.clear();
                            sf::Uint32 new_token;
                            std::string code;
                            do 
                            {
                                code = "";
                                code.append(std::to_string(rand() % 9 + 1));
                                for (int i = 0; i < 5; ++i)
                                    code.append(std::to_string(rand() % 10));
                                new_token = std::stoi(code);
                            }
                            while (games.count(new_token));
                            std::cout << username.toAnsiString() << " " << new_token << std::endl;
                            if (!games.count(new_token))
                            {
                                games[new_token].users.push_back(User{
                                    username, (*it).first, 0, 100, rank, 0.f, true, sf::Vector2f(0.f, 500.f), it->second, avatar, id 
                                    });
                                std::cout << username.toAnsiString() << " utworzyl poczekalnie#" << new_token << " sloty " <<
                                    games[new_token].users.size() << "/2" << std::endl;
                            }
                            it->second = new_token;
                            pak.clear();
                            pak << new_token;
                            if ((*it).first->send(pak) != sf::Socket::Done)
                                std::cout << "NIE WYSLANO GAMETOKENA"; 
                        }
                            break;
                        case 4://join lobby
                        {
                            sf::Uint32 token;
                            sf::String username;
                            std::string avatar;
                            int id;
                            
                            int rank;
                            pak >> token >> username >> rank >> avatar >> id;
                            
                            if (games.count(token) && !games[token].in_game && games[token].users.size() == 1)
                            {
                                games[token].users.push_back(User{
                                    username, (*it).first, 0, 100, rank, 0.f, false, sf::Vector2f(700.f, 500.f),
                                    it->second, avatar, id
                                    });
                                std::cout << username.toAnsiString() << " dolaczyl do poczekalni#" << token << " sloty " <<
                                    games[token].users.size() << "/2" << std::endl;
                                pak.clear();
                                it->second = token;
                                //SEND TO GUEST
                                pak << games[token].users[0].nick << games[token].users[0].rank << games[token].users[0].avatar;
                                if (games[token].users[1].socket->send(pak) != sf::Socket::Done)
                                    std::cout << "NIE WYSLANO DANYCH DO GUESTA";
                                //SEND TO HOST
                                pak.clear();
                                pak << (sf::Uint32)2 << games[token].users[1].nick << games[token].users[1].rank << games[token].users[1].avatar;
                                if (games[token].users[0].socket->send(pak) != sf::Socket::Done)
                                    std::cout << "NIE WYSLANO DANYCH DO HOSTA";
                            }
                        }
                            break;
                        case 5://leave lobby
                        {
                            it->second = 0;
                            sf::Uint32 token;
                            pak >> token;
                            leaveGame(it->first, token,games);
                        }
                            break;
                        case 6://toggle ready
                        {
                            sf::Uint32 token;
                            pak >> token;
                            games[token].second_ready = !games[token].second_ready;
                            pak.clear();
                            pak << (sf::Uint32)3 << games[token].second_ready;
                            if (games[token].users[1].socket->send(pak) != sf::Socket::Done)
                                std::cout << token << "NIE WYSLANO DANYCH O TOGGLE DO GUESTA\n";
                            if (games[token].users[0].socket->send(pak) != sf::Socket::Done)
                                std::cout << token << "NIE WYSLANO DANYCH O TOGGLE DO HOSTA\n";
                        }
                            break;
                        case 7://run game
                        {
                            sf::Uint32 token;
                            int skin;
                            pak >> token;
                            if (games[token].second_ready && games[token].users.size() > 1)
                            {
                                games[token].in_game = true;
                                pak.clear();
                                pak << (sf::Uint32)4 << 
                                    games[token].users[0].pos.x <<
                                    games[token].users[0].pos.y << 
                                    games[token].users[0].hp << 
                                    games[token].users[0].right_direction << 
                                    games[token].users[0].skin << 
                                    games[token].users[0].weapon_rotation
                                    << games[token].users[1].pos.x << games[token].users[1].pos.y
                                    << games[token].users[1].hp << games[token].users[1].right_direction
                                    << games[token].users[1].skin << games[token].users[1].weapon_rotation;
                                if (games[token].users[1].socket->send(pak) != sf::Socket::Done)
                                    std::cout << token << "NIE WYSLANO DANYCH O STARCIE GRY DO GUESTA\n";
                                if (games[token].users[0].socket->send(pak) != sf::Socket::Done)
                                    std::cout << token << "NIE WYSLANO DANYCH O STARCIE GRY DO HOSTA\n";
                            }
                        }
                            break;
                        case 8://in game get pos and stuff
                        {
                            sf::Uint32 token;
                            int bulletnr, client, anotherone, hpp;
                            float x, y;
                            pak >> token;
                            if (games.count(token))
                            {
                                if (games[token].error || games[token].users.size() < 2 && games[token].in_game)
                                {
                                    std::cout << "\nERROR : " << games[token].error << "\n"; \
                                        std::cout << "\nPLAYERS : " << games[token].users.size() << "\n"; \
                                        std::cout << "\IN GAME : " << games[token].in_game << "\n"; \

                                        pak.clear();
                                    pak << false << -10 << -10;
                                    it->first->send(pak);
                                    games.erase(token);
                                    it->second = 0;
                                }
                                else
                                {
                                    if (!games[token].in_game)
                                    {
                                        pak.clear();
                                        if (games[token].users[0].socket == it->first)
                                            pak << false << games[token].users[0].hp << games[token].users[1].hp;
                                        else
                                            pak << false << games[token].users[1].hp << games[token].users[0].hp;
                                        it->first->send(pak);
                                        games[token].finished++;
                                        if (games[token].finished >= 2)
                                            games.erase(token);
                                        it->second = 0;
                                    }
                                    else
                                    {
                                        if (it->first == games[token].users[0].socket)
                                            client = 0;
                                        else
                                            client = 1;
                                        anotherone = (client == 1 ? 0 : 1);
                                        pak >> games[token].users[client].pos.x >>
                                            games[token].users[client].pos.y >>
                                            hpp >>
                                            games[token].users[client].right_direction >>
                                            games[token].users[client].weapon_rotation >>
                                            bulletnr;
                                        games[token].users[client].bullets.clear();
                                        for (int i = 0; i < bulletnr; i++)
                                        {
                                            pak >> x >> y;
                                            if (x < 0 || x > 800.f
                                                || y < 0 || y > 600.f)
                                            {
                                                x = 1000;
                                                y = 1000;
                                            }


                                            for (auto& j : obstacles)
                                            {
                                                if (j.intersects(sf::FloatRect({ x,y }, { 8,8 })))
                                                {
                                                    x = 1000;
                                                    y = 1000;
                                                }
                                            }
                                            if (sf::FloatRect(games[token].users[anotherone].pos, { 100,100 }).intersects(sf::FloatRect({ x,y }, { 8,8 })))
                                            {
                                                games[token].users[anotherone].hp -= 4;
                                                x = 1000;
                                                y = 1000;
                                                if (games[token].users[anotherone].hp <= 0)
                                                {
                                                    //games.erase(token); 
                                                    games[token].users[anotherone].hp = 0;
                                                    games[token].in_game = false;
                                                    games[token].hp.x = games[token].users[0].hp;
                                                    games[token].hp.y = games[token].users[1].hp;
                                                    
                                                    //give money,xp,points,wins,losses to players
                                                    int winner = games[token].hp.x > 0 ? 0 : 1,
                                                        loser = winner == 1 ? 0 : 1;
                                                    CURL* curl;
                                                    CURLcode res;
                                                    std::string readBuffer = "";
                                                    std::string url = "localhost:13378/api/game/increaseuserdatabyid?token=haselkomaselko&id="
                                                        + std::to_string(games[token].users[winner].id) + "&money=10&xp=20&wins=1&points=10";
                                                    curl = curl_easy_init();
                                                    if (curl) {
                                                        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                                                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                                                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                                                        res = curl_easy_perform(curl);
                                                        curl_easy_cleanup(curl);
                                                    }
                                                    url = "localhost:13378/api/game/increaseuserdatabyid?token=haselkomaselko&id="
                                                        + std::to_string(games[token].users[loser].id) + "&xp=5&losses=1&points=-10";
                                                    curl = curl_easy_init();
                                                    if (curl) {
                                                        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                                                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                                                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                                                        res = curl_easy_perform(curl);
                                                        curl_easy_cleanup(curl);
                                                    }
                                                    break;
                                                }
                                            }
                                            games[token].users[client].bullets.push_back({ x,y });
                                        }
                                        pak.clear();
                                        bulletnr = games[token].users[anotherone].bullets.size();
                                        pak << true << games[token].users[anotherone].pos.x <<
                                            games[token].users[anotherone].pos.y <<
                                            games[token].users[anotherone].hp <<
                                            games[token].users[anotherone].right_direction <<
                                            games[token].users[anotherone].weapon_rotation <<
                                            games[token].users[client].hp <<
                                            bulletnr;
                                        for (int i = 0; i < bulletnr; i++)
                                            pak << games[token].users[anotherone].bullets[i].x << games[token].users[anotherone].bullets[i].y;
                                        bulletnr = games[token].users[client].bullets.size();
                                        pak << bulletnr;
                                        for (int i = 0; i < bulletnr; i++)
                                            pak << games[token].users[client].bullets[i].x << games[token].users[client].bullets[i].y;
                                        it->first->send(pak);
                                    }
                                }
                            } 
                        }
                            break;
                        case 9://set skin
                        {
                            sf::Uint32 token;
                            int skin;
                            pak >> token >> skin;
                            if (it->first == games[token].users[0].socket)
                            {
                                games[token].users[0].skin = skin;
                                std::cout<<"FOR HOST SKIN: " << skin << '\n';
                            }
                            else
                            {
                                games[token].users[1].skin = skin;
                                std::cout << "FOR GUEST SKIN: " << skin << '\n';
                            }
                                
                        }
                            break;
                        case 10://get data to profile state
                        {
                            int id;
                            pak >> id;
                            CURL* curl;
                            CURLcode res;
                            std::string readBuffer = "";
                            std::string url = "localhost:13378/api/game/getuserdatabyid?id="+std::to_string(id);
                            curl = curl_easy_init();
                            if (curl) {
                                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                                res = curl_easy_perform(curl);
                                curl_easy_cleanup(curl);
                            }
                            json j = json::parse(readBuffer);
                            sf::String name = j["data"]["display_name"].get<sf::String>(),
                                desc = j["data"]["description"].get<sf::String>(),
                                img = j["data"]["img"].get<sf::String>();
                            int rank = j["data"]["rank"].get<int>(),
                                money = j["data"]["money"].get<int>(),
                                xp = j["data"]["xp"].get<int>(),
                                points = j["data"]["points"].get<int>(),
                                wins = j["data"]["wins"].get<int>(),
                                losses = j["data"]["losses"].get<int>();
                            id = j["data"]["id"].get<int>();
                            url = img;
                            readBuffer = "";
                            curl = curl_easy_init();
                            if (curl) {
                                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                                res = curl_easy_perform(curl);
                                curl_easy_cleanup(curl);
                            }
                            pak.clear();
                            pak << 0 << id << name << desc << rank << money << readBuffer << xp << points << wins << losses;
                            if (it->first->send(pak) != sf::Socket::Done)
                                std::cout << "nie mozna wyslac pakietu";
                        }
                            break;
                        case 11://get leaderboard
                            CURL* curl;
                            CURLcode res;
                            std::string readBuffer = "";
                            std::string url = "localhost:13378/api/game/getleaderboard?";
                            curl = curl_easy_init();
                            if (curl) {
                                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                                res = curl_easy_perform(curl);
                                curl_easy_cleanup(curl);
                            }
                            json j = json::parse(readBuffer);
                            int size = j["data"].size();
                            pak.clear();
                            pak << size;
                            for (int i = 0; i < size; i++)
                            {
                                sf::String name = j["data"][i]["display_name"].get<sf::String>(),
                                    desc = j["data"][i]["description"].get<sf::String>(),
                                    img = j["data"][i]["img"].get<sf::String>();
                                int rank = j["data"][i]["rank"].get<int>(),
                                    money = j["data"][i]["money"].get<int>(),
                                    xp = j["data"][i]["xp"].get<int>(),
                                    points = j["data"][i]["points"].get<int>(),
                                    wins = j["data"][i]["wins"].get<int>(),
                                    losses = j["data"][i]["losses"].get<int>(),
                                    id = j["data"][i]["id"].get<int>();
                                url = img;
                                readBuffer = "";
                                curl = curl_easy_init();
                                if (curl) 
                                {
                                    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                                    res = curl_easy_perform(curl);
                                    curl_easy_cleanup(curl);
                                }
                               
                                pak << name << desc << rank << money << readBuffer << xp << points << wins << losses << id;
                            }
                            if (it->first->send(pak) != sf::Socket::Done)
                                std::cout << "nie mozna wyslac pakietu";
                            break;
                        } 
                    }
                    else if (st == sf::Socket::Disconnected)
                    {
                        if (it->second != 0)
                        {
                            leaveGame(it->first, it->second,games);
                        }
                        std::cout << "User disconnected\n";
                        sel.remove(*(*it).first);
                        delete (*it).first;
                        it = clients.erase(it);
                        break;
                    }
                }

            }
        }
    }
    cls.join();
}