#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <map>
#include <sys/wait.h>
#include <unistd.h>
#include <memory>
#include <sstream>

using boost::asio::ip::tcp;

std::map<std::string,std::string> ParseHttpRequest(std::string request)
{
    using namespace std;
    map<string,string> request_table;
    istringstream iss(request);
    iss >> request_table["REQUEST_METHOD"] >> request_table["REQUEST_URI"] >> request_table["SERVER_PROTOCOL"] >> request >> request_table["HTTP_HOST"];
    iss = istringstream(request_table["REQUEST_URI"]);
    getline(iss,request_table["REQUEST_URI"],'?');
    getline(iss,request_table["QUERY_STRING"]);
    request_table["EXEC_FILE"] = request_table["REQUEST_URI"].substr(1);

    for(auto i:request_table)
        cout<<i.first<<" "<<i.second<<endl;
    cout<<"----------------------------------------"<<endl;

    return request_table;
}

class HttpServer
{
public:
    HttpServer(boost::asio::io_context& io_context, short port):
        io_context_(io_context),
        acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        cli_socket_(io_context_){
            do_accept();
        }
private:
    void do_accept();
    void do_read();

    boost::asio::io_context &io_context_;
    tcp::acceptor acceptor_;
    tcp::socket cli_socket_;
    char data_[2048];
};

void HttpServer::do_accept()
{
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket cli_socket)
        {
            if (!ec)
            {
                cli_socket_ = std::move(cli_socket);
                io_context_.notify_fork(boost::asio::io_context::fork_prepare);

                if(fork()==0) //child process 
                {
                    io_context_.notify_fork(boost::asio::io_context::fork_child);
                    acceptor_.close(); //In child process, it don't need to accept new connection
                    do_read();
                }
                else
                {
                    io_context_.notify_fork(boost::asio::io_context::fork_parent);
                    cli_socket_.close();
                    do_accept();
                }
            }// end of if(!ec)
        }//end of lambda expression
    );// end of _async_accept
}

void HttpServer::do_read()
{
    cli_socket_.async_read_some(boost::asio::buffer(data_),
        [this](boost::system::error_code ec,std::size_t length)
        {
            if(!ec)
            {
                //parse http request and set environment variable
                std::string request(data_);
                auto request_table = ParseHttpRequest(request);
                setenv("REQUEST_METHOD",request_table["REQUEST_METHOD"].c_str(), 1);
                setenv("REQUEST_URI", request_table["REQUEST_URI"].c_str(), 1);
                setenv("QUERY_STRING", request_table["QUERY_STRING"].c_str(), 1);
                setenv("SERVER_PROTOCOL", request_table["SERVER_PROTOCOL"].c_str(), 1);
                setenv("HTTP_HOST", request_table["HTTP_HOST"].c_str(), 1);
                setenv("SERVER_ADDR",cli_socket_.local_endpoint().address().to_string().c_str(),1);
                setenv("SERVER_PORT",std::to_string(cli_socket_.local_endpoint().port()).c_str(),1);
                setenv("REMOTE_ADDR",cli_socket_.remote_endpoint().address().to_string().c_str(),1);
                setenv("REMOTE_PORT",std::to_string(cli_socket_.remote_endpoint().port()).c_str(),1);

                //dup
                dup2(cli_socket_.native_handle(), 0);
                dup2(cli_socket_.native_handle(), 1);
                dup2(cli_socket_.native_handle(), 2);

                //exec
                std::cout << "HTTP/1.1" << " 200 OK\r\n";
                std::cout.flush();
                char *argv[] = {nullptr};
                if(execv(request_table["EXEC_FILE"].c_str(), argv) == -1)
                {
                    std::cerr << "execv error";
                    exit(-1);
                }
            }
        });
}

int main(int argc, char *argv[])
{
    using namespace std;
    signal(SIGCHLD, SIG_IGN); //prevent zombie process
    int port;
    if(argc==1)
    {
        cerr<<"Please enter port number to open the server"<<endl;
        exit(1);
    }
    else if(argc==2)
    {
        port = atoi(argv[1]);
        if(port==0)
        {
            cerr<<"Invalid port number!"<<endl;
            exit(1);
        }
    }

    boost::asio::io_context io_context;
    HttpServer server(io_context,port);
    io_context.run();
}