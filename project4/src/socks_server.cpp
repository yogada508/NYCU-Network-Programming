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
#include <fstream>
#include <sstream>

using namespace std;
using boost::asio::ip::tcp;
boost::asio::io_context io_context;

class SocksServer
{
public:
    SocksServer(boost::asio::io_context& io_context, short port):
        io_context_(io_context),
        acceptor_(io_context, tcp::endpoint(tcp::v4(), port)){
            do_accept();
        }

private:
    void do_accept();
    void do_read();
    void ParseSocksRequest(int length);
    bool PassFirewall(int command);
    void ReplyReject();
    void ReplyConnect();
    void ReplyBind();
    void ReadFromDst();
    void WriteToSrc(int length);
    void ReadFromSrc();
    void WriteToDst(int length);
    void PrintInformation(string reply);

    boost::asio::io_context &io_context_;
    tcp::acceptor acceptor_;
    tcp::acceptor bind_mode_acceptor{io_context_};
    tcp::resolver resolver_{io_context_};
    tcp::socket source_socket{io_context_}; //the socket connect to browser (client) 
    tcp::socket destination_socket{io_context_}; // the socket connect to website (server) (must use "io_context" in self class )
    unsigned char data[4096];
    array<unsigned char, 65536> data_from_dst{};
    array<unsigned char, 65536> data_from_src{};
    map<string,string> information;
};

void SocksServer::do_accept()
{
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket cli_socket)
        {
            if (!ec)
            {
                source_socket = std::move(cli_socket);
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
                    source_socket.close();
                    destination_socket.close();
                    do_accept();
                }
            }// end of if(!ec)
        }//end of lambda expression
    );// end of _async_accept   
}

void SocksServer::do_read()
{
    source_socket.async_read_some(boost::asio::buffer(data),
        [this](boost::system::error_code ec,std::size_t length)
        {
            if (!ec)
            {
                /*for(size_t i = 0; i<length; i++)
                    cout <<int(data[i])<<" ";
                cout<<endl;*/
                string reply;
                ParseSocksRequest(length);
                if(int(data[0]) != 4 || information["dst_ip"] == "0.0.0.0" || !PassFirewall(int(data[1])))
                {
                    reply = "Reject";
                    PrintInformation(reply);
                    ReplyReject();
                }
                else if(information["command"] == "CONNECT")
                {
                    reply = "Accept";
                    PrintInformation(reply);
                    ReplyConnect();

                }
                else if(information["command"] == "BIND")
                {
                    reply = "Accept";
                    PrintInformation(reply);
                    ReplyBind();
                }
                
            }
        });
}

void SocksServer::ParseSocksRequest(int length)
{
    information["command"] = (int(data[1]) == 1) ? "CONNECT" : "BIND";
    information["dst_port"] = to_string(int(data[2]) * 256 + int(data[3]));
    
    //record destination ip
    string temp_ip;
    for(int i = 4; i<8; i++)
    {
        if(i != 4)
            temp_ip += ".";
        int value = int(data[i]);
        temp_ip += to_string(value);
    }
    information["dst_ip"] = temp_ip;

    //recode domain name of the destination
    information["domain_name"] = "";
    bool socks4a = false;
    for(int i = 8; i<length; i++)
    {
        if(int(data[i]) == 0 && i != length-1)
        {
            socks4a = true;
            continue;      
        }
        if(socks4a && i != length -1)
        {
            information["domain_name"] += data[i];
        }
    }

    //if use request is socks4a, the server need to resolve the domain name
    if(information["domain_name"] != "")
    {
        tcp::resolver resolver(io_context);
		tcp::resolver::query query(information["domain_name"], information["dst_port"]);
		tcp::resolver::iterator iter = resolver.resolve(query);
		tcp::endpoint endpoint = *iter;
        information["dst_ip"] = endpoint.address().to_string();
    }
}

bool SocksServer::PassFirewall(int command)
{
    ifstream file_stream("./socks.conf");
    string line;

    while(getline(file_stream, line))
    {
        std::stringstream ss(line);
        string action, operation, ip;

        ss >> action >> operation >> ip;

        if (action == "permit" && ((operation == "c" && command == 1) || (operation == "b" && command == 2)))
        {
            auto prefix = ip.substr(0, ip.find('*'));
            if (information["dst_ip"].substr(0, prefix.size()) == prefix)
            {
                file_stream.close();
                return true;
            }
        }
        
    }
    file_stream.close();
    return false;
}

void SocksServer::ReplyReject()
{
    unsigned char reply[8]={0};
    reply[1] = 91;

    boost::asio::async_write(source_socket,boost::asio::buffer(reply),
        [this](boost::system::error_code ec, std::size_t)
        {
            if(ec)
                return;
        });
}

void SocksServer::ReplyConnect()
{
    unsigned char reply[8]={0};
    reply[1] = 90;

    boost::asio::async_write(source_socket,boost::asio::buffer(reply),
        [this](boost::system::error_code ec, std::size_t)
        {
            if(!ec)
            {
                tcp::endpoint endpoint(boost::asio::ip::address::from_string(information["dst_ip"]), atoi(information["dst_port"].c_str()));
                destination_socket.connect(endpoint);
                ReadFromDst();
                ReadFromSrc();
            }
        });
    
}

void SocksServer::ReplyBind()
{
    unsigned char reply[8]={0};
    reply[1] = 90;

    tcp::endpoint endpoint(tcp::v4(), 0);
    bind_mode_acceptor.open(endpoint.protocol());
    bind_mode_acceptor.set_option(tcp::acceptor::reuse_address(true));
    bind_mode_acceptor.bind(endpoint);
    bind_mode_acceptor.listen();

    reply[2] = bind_mode_acceptor.local_endpoint().port() / 256;
    reply[3] = bind_mode_acceptor.local_endpoint().port() % 256;

    boost::asio::async_write(source_socket,boost::asio::buffer(reply),
        [this,reply](boost::system::error_code ec, std::size_t)
        {
            if(!ec)
            {
                bind_mode_acceptor.accept(destination_socket);
                boost::asio::write(source_socket, boost::asio::buffer(reply));
                ReadFromDst();
                ReadFromSrc();
            }
        });

}

void SocksServer::ReadFromDst()
{
    destination_socket.async_read_some(boost::asio::buffer(data_from_dst),
        [this](boost::system::error_code ec, std::size_t length)
        {
            if(!ec)
            {
                WriteToSrc(length);
            }

            //when the peer endpoint close the connection, we will receive eof error
            //In bind mode, after ftp server transmit file, it will close the connection, so we need to close the socket too.
            else if(ec == boost::asio::error::eof)
            {
                source_socket.close();
                destination_socket.close();
                return;
            }
            
        });
}

void SocksServer::WriteToSrc(int length)
{
    //must use async_write, if use async_write_some, we will get unexpected result.
    boost::asio::async_write(source_socket,boost::asio::buffer(data_from_dst,length),
        [this,length](boost::system::error_code ec, std::size_t)
        {
            if(!ec)
            {
                ReadFromDst();
            }
        });
}

void SocksServer::ReadFromSrc()
{
    source_socket.async_read_some(boost::asio::buffer(data_from_src),
        [this](boost::system::error_code ec, std::size_t length)
        {
            if(ec == boost::asio::error::eof)
            {
                source_socket.close();
                destination_socket.close();
                return;
            }
            if(!ec)
            {
                WriteToDst(length);
            }
        });
}

void SocksServer::WriteToDst(int length)
{
    boost::asio::async_write(destination_socket,boost::asio::buffer(data_from_src,length),
        [this](boost::system::error_code ec, std::size_t)
        {
            if(!ec)
            {
                ReadFromSrc();
            }
        });
}

void SocksServer::PrintInformation(string reply)
{
    cout<<"<S_IP>: "<<source_socket.remote_endpoint().address().to_string()<<endl;
    cout<<"<S_PORT>: "<<to_string(source_socket.remote_endpoint().port())<<endl;
    cout<<"<D_IP>: "<<information["dst_ip"]<<endl;
    cout<<"<D_PORT>: "<<information["dst_port"]<<endl;
    cout<<"<Command>: "<<information["command"]<<endl;
    cout<<"<Reply>: "<<reply<<endl;
    cout<<"------------------------------------------------------------"<<endl;
}

int main(int argc, char *argv[])
{
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
    SocksServer server(io_context,port);
    io_context.run();
}