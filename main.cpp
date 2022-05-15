#include "pch.h"
using namespace sf;
using json = nlohmann::json;


struct User
{
    sf::String nick;
    sf::TcpSocket* socket;
    int skin, //0 - cat, 1 - dog
    lives,// lives 0-3
    rank; 
    float weapon_rotation;
    bool right_direction;
    sf::Vector2f pos;
};

struct SessionData
{
    bool in_game = false, second_ready = false;
    std::vector<User> users;
    std::vector<sf::Vector2f/*pos*/>bullets;
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
        std::cout << readBuffer;
        pak << 10 << sf::String(readBuffer);
        if (socket->send(pak) != sf::Socket::Done)
            std::cout << "nie mozna wyslac pakietu ?? why??";
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
            std::cout << "nie mozna wyslac pakietu ?? why??";
        return 0;
    } 

    readBuffer.clear();
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, std::string("127.0.0.1:13378/api/game/getuserdatabysecret?token=" +
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
            std::cout << "nie mozna wyslac pakietu ?? why??";
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
            std::cout << "nie mozna wyslac pakietu ?? why??";
        return 0;
    }

    j = json::parse(readBuffer);
    sf::String name = j["data"]["display_name"].get<sf::String>(),
        desc = j["data"]["description"].get<sf::String>();
    int rank = j["data"]["rank"].get<int>(),
        money = j["data"]["money"].get<int>();
    sf::String img = j["data"]["img"].get<sf::String>();
    std::cout << std::string(name) << " " << std::string(desc) << " " <<
        rank << " " << money << std::endl << std::string(img);
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
    //std::cout << std::endl <<  readBuffer;

    pak.clear();
    pak << 0 << name << desc << rank << money << readBuffer;
    if (socket->send(pak) != sf::Socket::Done)
        std::cout << "nie mozna wyslac pakietu ?? why??";
    return 0;
}

void leaveGame(sf::TcpSocket* socket, int token, std::unordered_map<int /*token 6 digits*/, SessionData>& games)
{
    sf::Packet pak;
    if (games[token].in_game)//trwa gra
    {
        // koniec gry
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
            games.erase(token);
        }
        else//wychodzi guest
        {
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
    
    //Containers
    std::unordered_map<int /*token 6 digits*/, SessionData>games;

    //SOCKETS
    sf::TcpListener listener;
    std::vector<std::pair<sf::TcpSocket*, int /*game token, 0 - null*/>> clients;
    std::vector<std::unique_ptr<std::thread>>threads;
    sf::SocketSelector sel;
    if (listener.listen(307) != sf::Socket::Done)
    {
        std::cout << "Nie mogê rozpocz¹æ nas³uchiwania na porcie " << 307 << std::endl;
        exit(1);
    }
    else std::cout << "Slucham na porcie " << 307 << std::endl;
    sel.add(listener);

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
                            int rank;
                            pak >> username >> rank;
                            pak.clear();
                            sf::Uint32 new_token;
                            do 
                            {
                                new_token = rand() % 888888 + 111111;
                            }
                            while (games.count(new_token));
                            std::cout << username.toAnsiString() << " " << new_token << std::endl;
                            if (!games.count(new_token))
                            {
                                games[new_token].users.push_back(User{
                                    username, (*it).first, 0, 3, rank, 0.f, true, sf::Vector2f(0.f, 500.f)
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
                            int rank;
                            pak >> token >> username >> rank;
                            if (games.count(token) && !games[token].in_game && games[token].users.size() == 1)
                            {
                                games[token].users.push_back(User{
                                    username, (*it).first, 0, 3, rank, 0.f, false, sf::Vector2f(700.f, 500.f)
                                    });
                                std::cout << username.toAnsiString() << " dolaczyl do poczekalni#" << token << " sloty " <<
                                    games[token].users.size() << "/2" << std::endl;
                                pak.clear();
                                it->second = token;
                                //SEND TO GUEST
                                pak << games[token].users[0].nick << games[token].users[0].rank;
                                if (games[token].users[1].socket->send(pak) != sf::Socket::Done)
                                    std::cout << "NIE WYSLANO DANYCH DO GUESTA";
                                //SEND TO HOST
                                pak.clear();
                                pak << (sf::Uint32)2 << games[token].users[1].nick << games[token].users[1].rank;
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
                        } 
                    }
                    else if (st == sf::Socket::Disconnected)
                    {
                        if (it->second != 0)
                        {
                            leaveGame(it->first, it->second,games);
                        }
                        std::cout << "byebye :D";
                        sel.remove(*(*it).first);
                        delete (*it).first;
                        it = clients.erase(it);
                        break;
                    }
                }

            }
        }
    }
}