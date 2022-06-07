#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <iostream>

using namespace std;
boost::asio::io_context io_context;

void PrintHTML(string msg)
{
    string contentType = "Content-type: text/html\r\n\r\n";
    string temp = "<!DOCTYPE html>\
                    <html>\
                        <head>\
                            <meta charset=\"utf-8\">\
                            <title>My test page</title>\
                        </head>\
                        <body>";
    temp += "<h1>" + msg + "</h1>";
    temp += "</body></html>";
    cout<<contentType<<temp<<endl;
    fflush(stdout);
}

//split the string by the delimiter
vector<string> SplitString(const string &input, string delimiter)
{
    string temp = input;
    vector<string> result;

    char * strs = new char[input.length() + 1];
	strcpy(strs, input.c_str());   

	char * d = new char[delimiter.length() + 1];  
	strcpy(d, delimiter.c_str());  

	char *p = strtok(strs, d);  
	while(p) {  
		string s = p; 
		result.push_back(s);  
		p = strtok(NULL, d);  
	}  

    return result;

}

struct RWGHost
{
    string hostname = "";
    string port = "";
    string file = "";
};

class Console
{
public:
    Console() = default;
    void ParseQueryString();
    void Connect2Host();
    void InitialHtml();
    void Run();

    vector<RWGHost> host_list;

private:
    
};

class Client2RWG : public std::enable_shared_from_this<Client2RWG>
{
public:
    Client2RWG() = default;
    Client2RWG(string session_name, string file_name, boost::asio::ip::tcp::resolver::query query)
        :session_name(session_name), file_name(file_name),query(move(query)){
            file.open("test_case/" + file_name, ios::in);
        }
    
    void do_resolve();
    void do_connect(boost::asio::ip::tcp::resolver::iterator it);
    void do_write(string command);
    void do_read();

    string session_name;
    string file_name;
    fstream file;
    boost::asio::ip::tcp::resolver resolver{io_context};
    boost::asio::ip::tcp::socket socket{io_context};
    boost::asio::ip::tcp::resolver::query query;
    char data[4096];

private:
    void SendResponse(string session_name, string response);
    void SendCommand(string session_name, string command);
    void Encode2HTML(string &msg);
};

int main()
{
    Console console;
    console.ParseQueryString();
    console.Connect2Host();
    console.Run();
}

//parse the query string in envirment variable and store the host information.
void Console::ParseQueryString()
{
    string query = getenv("QUERY_STRING");
    vector<string> result = SplitString(query,"&");

    for(int i = 0; i<5; i++)
        host_list.push_back(RWGHost());

    for(string token:result)
    {
        string key = token.substr(0,token.find('='));
        string value = token.substr(key.size() + 1);
        if(!value.empty())
        {
            int index = key.back() - '0';
            char tag = key.front();
            if(tag == 'h')
                host_list[index].hostname= value;
            else if(tag == 'p')
                host_list[index].port= value;
            else if(tag == 'f')
                host_list[index].file= value;
        }
    }
}

void Console::Connect2Host()
{
    for(size_t i = 0; i<host_list.size(); i++)
    {
        if(host_list[i].hostname != "")
        {
            boost::asio::ip::tcp::resolver::query query(host_list[i].hostname, host_list[i].port);
            make_shared<Client2RWG>("s" + to_string(i), host_list[i].file, move(query))->do_resolve();
        }
    }
}

void Console::InitialHtml()
{
    string contentType;
	string initHTML;

	contentType = "Content-type: text/html\r\n\r\n";
	initHTML =
		"<!DOCTYPE html>\
		<html lang=\"en\">\
  			<head>\
    			<meta charset=\"UTF-8\" />\
    			<title>NP Project 3 Sample Console</title>\
    			<link\
      				rel=\"stylesheet\"\
      				href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\
      				integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\
      				crossorigin=\"anonymous\"\
    			/>\
    			<link\
      				href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\
      				rel=\"stylesheet\"\
    			/>\
    			<link\
      				rel=\"icon\"\
      				type=\"image/png\"\
      				href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\
    			/>\
    			<style>\
      				* {\
        				font-family: 'Source Code Pro', monospace;\
        				font-size: 1rem !important;\
      				}\
      				body {\
        				background-color: #212529;\
      				}\
      				pre {\
        				color: #cccccc;\
      				}\
      				b {\
        				color: #01b468;\
      				}\
    			</style>\
  			</head>\
  			<body>\
    			<table class=\"table table-dark table-bordered\">\
      				<thead>\
        				<tr>";
	for (size_t i = 0; i < host_list.size(); i++)
	{
        if(host_list[i].hostname != "")
		    initHTML += "<th scope=\"col\">" + host_list[i].hostname + ":" + host_list[i].port + "</th>";
	}

	initHTML += "</tr>\
      				</thead>\
      				<tbody>\
        				<tr>";

	for (size_t i = 0; i < host_list.size(); i++)
	{
        if(host_list[i].hostname != "")
		    initHTML += "<td><pre id = \"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>";
	}

	initHTML += 			"</tr>\
      				</tbody>\
    			</table>\
  			</body>\
		</html>";

	std::cout << contentType << initHTML << std::endl;
	fflush(stdout);
}

void Console::Run()
{
    InitialHtml();
    io_context.run();
}

void Client2RWG::do_resolve()
{
    auto self(shared_from_this());
    resolver.async_resolve(query,
        [this, self](const boost::system::error_code &ec,boost::asio::ip::tcp::resolver::iterator it) 
        {
            if (!ec)
            {
                do_connect(it);
            }
                
        });
}

void Client2RWG::do_connect(boost::asio::ip::tcp::resolver::iterator it)
{
    auto self(shared_from_this());
    socket.async_connect(*it,
        [this, self](boost::system::error_code ec)
        {
            if (!ec)
            {
                do_read();
            }
                
        });
}

void Client2RWG::do_read()
{
    auto self(shared_from_this());
    socket.async_read_some(boost::asio::buffer(data),
        [this, self](boost::system::error_code ec,std::size_t length)
        {
            if(!ec)
            {
                string response(data);
                memset(data, 0, 4096);
                SendResponse(session_name, response);
                if (response.find("% ") != std::string::npos)
                {
                    string command;
                    getline(file,command);
                    command += "\n";
                    SendCommand(session_name, command);
                    do_write(command);
                }
                do_read();
            }
        });
}

void Client2RWG::do_write(string command)
{
    auto self(shared_from_this());
    socket.async_write_some(boost::asio::buffer(command),
        [this, self, command](boost::system::error_code ec, std::size_t)
        {

        });
}

void Client2RWG::SendResponse(string session_name, string response)
{
    Encode2HTML(response);
    cout << "<script>document.getElementById(\'" + session_name + "\').innerHTML += \'" + response + "\';</script>";
	fflush(stdout);
}

void Client2RWG::SendCommand(string session_name, string command)
{   
    Encode2HTML(command);
    cout << "<script>document.getElementById(\'" + session_name + "\').innerHTML += \'<b>" + command + "</b>\';</script>";
    fflush(stdout);
}

void Client2RWG::Encode2HTML(string &msg)
{
    boost::replace_all(msg, "&", "&amp;");
    boost::replace_all(msg, "\"", "&quot;");
    boost::replace_all(msg, "\'", "&apos;");
    boost::replace_all(msg, "<", "&lt;");
    boost::replace_all(msg, ">", "&gt;");
    boost::replace_all(msg, "\n", "&NewLine;");
    boost::replace_all(msg, "\r", "");
}