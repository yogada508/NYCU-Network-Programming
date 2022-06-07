#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <stdlib.h>
#include <vector>
#include <string>
#include <string.h>
#include <unistd.h>
#include <cstring>
#include <iterator>
#include <sstream>
#include <map>
#include <algorithm>

using namespace std;

#define MAX_CLIENT_NUMBER 30
#define MAX_PIPE_NUM 99
#define MAX_PIPE_SIZE 1024*1024 //10M for pipe size
map<string,pair<int,int>> user_pipe_table;

struct NumpipeInfo
{
    int beginning_write_pipe = -1;
    int input_fd = STDIN_FILENO;
    int output_fd = STDOUT_FILENO;
    int error_fd = STDERR_FILENO;

};

struct CommandInfo
{
    vector<string> command_list;
    int pipe_count = 0;
    int numbered_pipe = 0;
    string file_redirection = "";
    bool pipe_stderr = false;

};

//remove leading and trailing spaces in string
void TrimString(string &input)
{
    if (!input.empty() && input[input.size() - 1] == '\r') // due to the end of telnet client message is "\r\n", we need to remove '\r' from string
        input.erase(input.size()-1);
    input.erase(input.find_last_not_of(" ")+1);
    input.erase(0,input.find_first_not_of(" "));
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

class UserPipeHandler
{
    public:
        UserPipeHandler() = default;
        UserPipeHandler(string command): command(command),read_from_null(false),write_to_null(false),
                            read_pipe_sender_uid(0),read_pipe_receiver_uid(0),write_pipe_sender_uid(0),write_pipe_receiver_uid(0){}                    
        string ParseCommand(int caller_uid, int &input_fd, int &output_fd, string original_cmd);

    private:
        string get_read_pipe_key(){return "#" + to_string(read_pipe_sender_uid) + "->#" + to_string(read_pipe_receiver_uid);}
        string get_write_pipe_key(){return "#" + to_string(write_pipe_sender_uid) + "->#" + to_string(write_pipe_receiver_uid);}

        string command;
        bool read_from_null;
        bool write_to_null;
        int read_pipe_sender_uid;
        int read_pipe_receiver_uid;
        int write_pipe_sender_uid;
        int write_pipe_receiver_uid;
};

class Client
{
    public:
        Client():socket_fd(-1),uid(0),user_name("(no name)"),ip(""),port(-1),cmd_number(1){}
        Client(int socket_fd, int uid, string ip, int port, string user_name): socket_fd(socket_fd), uid(uid), ip(ip), port(port), user_name(user_name){
            env_table["PATH"] = "bin:.";
        }

        void BackUp_std_fd();
        void restore_std_fd();
        void ExecuteCmd(const string &cmd);

        int socket_fd;
        int uid;
        string user_name;
        string ip;
        uint16_t port;
        int cmd_number; //record the command number from input. (include unknown command, but not include empty input)
        map<string,string> env_table;

    private:
        void inline ClearPipeList(){
            for(size_t i = 0; i<MAX_PIPE_NUM; i++) {pipe_list[i][0]=0; pipe_list[i][1]=0;}};

        bool inline FindKeyInNumbered_pipe_table(int key){        
            if (numbered_pipe_table.find(key) != numbered_pipe_table.end()) return true;
            else return false;};
        CommandInfo GetCommandInfo(const string &cmd);
        pid_t ForkAndExecute(string cmd, int pipe_count, int input_fd, int output_fd, int error_fd, string file_redirection);

        int backup_std_fd[3];
        map<int,NumpipeInfo> numbered_pipe_table;
        int pipe_list[MAX_PIPE_NUM][2];

};

//only when the server receive the message from client need to dup2 client_socket to STANDARD_FD,
//so we need to backup the STANDARD_FD before dup2
void Client::BackUp_std_fd()
{
    backup_std_fd[0] = dup(STDIN_FILENO);
    backup_std_fd[1] = dup(STDOUT_FILENO);
    backup_std_fd[2] = dup(STDERR_FILENO);
}

//after execute client's message we restore the STANDARD_FD
void Client::restore_std_fd()
{
    dup2(backup_std_fd[0], STDIN_FILENO);
    close(backup_std_fd[0]);
    dup2(backup_std_fd[1], STDOUT_FILENO);
    close(backup_std_fd[1]);
    dup2(backup_std_fd[2], STDERR_FILENO);
    close(backup_std_fd[2]);

}

void Client::ExecuteCmd(const string &cmd)
{
    CommandInfo command_info = GetCommandInfo(cmd);
    ClearPipeList();

    vector<pid_t> pid_list;

    //create numbered pipe if this input command need it.
    if(command_info.numbered_pipe > 0)
    {
        //if the numbered pipe target doesn't exist, create a new pipe and fork a process to store all messages
        if(!FindKeyInNumbered_pipe_table(cmd_number+command_info.numbered_pipe))
        {
            NumpipeInfo num_pipe_info;
            if(!FindKeyInNumbered_pipe_table(cmd_number))
                numbered_pipe_table[cmd_number] = num_pipe_info;

            numbered_pipe_table[cmd_number + command_info.numbered_pipe] = num_pipe_info;

            int pipe_fd[2];
            pipe(pipe_fd);
            fcntl(pipe_fd[0],F_SETPIPE_SZ,MAX_PIPE_SIZE);
            numbered_pipe_table[cmd_number].output_fd = pipe_fd[1];
            numbered_pipe_table[cmd_number + command_info.numbered_pipe].input_fd = pipe_fd[0];
            numbered_pipe_table[cmd_number + command_info.numbered_pipe].beginning_write_pipe = pipe_fd[1];
            if(command_info.pipe_stderr)
                numbered_pipe_table[cmd_number].error_fd = pipe_fd[1];
        }
        //if the numbered pipe target exist, use the existed pipe
        else
        {
            NumpipeInfo num_pipe_info;
            if(!FindKeyInNumbered_pipe_table(cmd_number))
                numbered_pipe_table[cmd_number] = num_pipe_info;

            numbered_pipe_table[cmd_number].output_fd = numbered_pipe_table[cmd_number + command_info.numbered_pipe].beginning_write_pipe;
        }
    }
    

    //check the input command needs batch_execute or not
    int start_fd = STDIN_FILENO;
    int end_fd = STDOUT_FILENO;
    int batch_read_fd = -1;
    if(command_info.pipe_count > MAX_PIPE_NUM)
    {
        if(!FindKeyInNumbered_pipe_table(cmd_number))
        {
            NumpipeInfo batch_pipe_info;
            numbered_pipe_table[cmd_number] = batch_pipe_info;
        }
        else //record the input command's input_fd and output_fd if this command is in table
        {
            start_fd = numbered_pipe_table[cmd_number].input_fd;
            end_fd = numbered_pipe_table[cmd_number].output_fd;

        }
    }
    
    for (size_t i = 0; i < command_info.command_list.size();)
    {
        ClearPipeList();
        vector<string> command_batch;
        if(command_info.pipe_count > MAX_PIPE_NUM)
        {
            numbered_pipe_table[cmd_number].input_fd = start_fd;
            numbered_pipe_table[cmd_number].beginning_write_pipe = batch_read_fd;
            int left_idx = i;
            int right_idx = (i+ MAX_PIPE_NUM < command_info.command_list.size())? i + MAX_PIPE_NUM : command_info.command_list.size() - 1;
            i = right_idx + 1;
            
            if(right_idx == command_info.command_list.size()-1) //last batch
                numbered_pipe_table[cmd_number].output_fd = end_fd;
            else
            {
                int batch_pipe[2];
                if(pipe(batch_pipe) == -1)
                    cerr<<"create pipe error\n";
                numbered_pipe_table[cmd_number].output_fd = batch_pipe[1];
                batch_read_fd = batch_pipe[1]; //for next command batch
                start_fd = batch_pipe[0]; //for next command batch
            }

            copy(command_info.command_list.begin()+left_idx,command_info.command_list.begin()+right_idx+1,back_inserter(command_batch));
        }
        else
        {
            command_batch = command_info.command_list;
            i = command_info.command_list.size();
        }

        int command_batch_pipe_cnt = command_batch.size()-1;
        for (size_t j = 0; j < command_batch_pipe_cnt; j++)
        {
            if(pipe(pipe_list[j]) == -1)
                cerr<<"create pipe error\n";
        }

        //execute each subcommand
        int input_fd = STDIN_FILENO;
        int output_fd = STDOUT_FILENO;
        int error_fd = STDERR_FILENO;
        bool cmd_in_map = FindKeyInNumbered_pipe_table(cmd_number);
        for (size_t j = 0; j < command_batch.size(); j++)
        {
            if(cmd_in_map)
            {
                input_fd = (j == 0) ? numbered_pipe_table[cmd_number].input_fd : pipe_list[j - 1][0];
                output_fd = (j == command_batch.size() - 1) ? numbered_pipe_table[cmd_number].output_fd : pipe_list[j][1];
                if(command_info.pipe_stderr)
                    error_fd = (j == command_batch.size() - 1) ? numbered_pipe_table[cmd_number].error_fd : pipe_list[j][1];
                else
                    error_fd = STDERR_FILENO;
            }
            else
            {
                input_fd = (j == 0) ? STDIN_FILENO : pipe_list[j - 1][0];
                output_fd = (j == command_batch.size() - 1) ? STDOUT_FILENO : pipe_list[j][1];
            }

            UserPipeHandler handler(command_batch[j]);
            string cmd_parsed = handler.ParseCommand(uid,input_fd,output_fd,cmd);

            pid_t child_pid = ForkAndExecute(cmd_parsed,command_batch_pipe_cnt,input_fd,output_fd,error_fd,command_info.file_redirection);
            pid_list.push_back(child_pid);

            //if this command line is piped from previous command line, close the write and read pipe in the parent.
            if(cmd_in_map)
            {
                if(input_fd == numbered_pipe_table[cmd_number].input_fd && input_fd != STDIN_FILENO)
                {
                    close(input_fd);
                    close(numbered_pipe_table[cmd_number].beginning_write_pipe);
                }
            }
            
            //close user pipe.
            for(auto iter=user_pipe_table.begin(); iter!=user_pipe_table.end();)
            {
                vector<string> temp = SplitString(iter->first,"->");
                string sender_uid = temp[0];
                string receiver_uid = temp[1];
                sender_uid.erase(0,1);
                receiver_uid.erase(0,1);
                if((stoi(receiver_uid) == uid))
                {
                    if(input_fd == iter->second.first)
                    {
                        close(iter->second.first);
                        close(iter->second.second);
                        user_pipe_table.erase(iter++);
                        continue;
                    }
                }
                iter++;
            }
        }

        //in parent process, close all pipe between each subcommad after execute the command
        for (size_t j = 0; j < command_batch_pipe_cnt; j++) 
        {
            close(pipe_list[j][0]);
            close(pipe_list[j][1]);
        }
        for (const auto &j:pid_list)
        {
            waitpid(j,nullptr,0);
        }
    }

}

CommandInfo Client::GetCommandInfo(const string &cmd)
{
    string temp = cmd;
    CommandInfo info;

    vector<string> after_split = SplitString(temp," ");

    string sub_cmd = "";
    for(size_t i = 0; i<after_split.size(); i++)
    {
        bool store_sub_cmd = true;
        bool end = false;

        if(after_split[i] == "|")
            info.pipe_count++;
        else if(after_split[i] == ">")
        {
            info.file_redirection = after_split[i+1];
            end = true;
        }
            
        else if(after_split[i].size() > 1 && (after_split[i][0] == '|' || after_split[i][0] == '!'))
        {
            if(after_split[i][0] == '!')
                info.pipe_stderr = true;
            info.numbered_pipe = atoi(after_split[i].substr(1).c_str());
        }
        else
        {
            store_sub_cmd = false;
            sub_cmd += after_split[i] + " ";
        }

        //store the sub command
        if(store_sub_cmd || (i == after_split.size()-1 && sub_cmd != "")) //when reach the end of vector
        {
            info.command_list.push_back(sub_cmd.substr(0,sub_cmd.size()-1)); //remove the last space of command
            sub_cmd = "";
        }
        if(end)
            break;
    }

    return info;
}

pid_t Client::ForkAndExecute(string cmd_parsed, int pipe_count, int input_fd, int output_fd, int error_fd, string file_redirection)
{

    pid_t child_pid;
    while ((child_pid = fork()) < 0) //if fork error (i.e., too many process is running), wait 2ms to fork again
        usleep(2000);

    if(child_pid == 0) //child process
    {
        //cout<<"pid "<<getpid()<<": "<<cmd_parsed<<" "<<input_fd<<" "<<output_fd<<" "<<error_fd<<endl; 

        if (input_fd != STDIN_FILENO) 
        {
            if (dup2(input_fd, STDIN_FILENO) == -1)
                cerr<<cmd_parsed<<" dup error (input_fd)\n";

            //if the input of this subcommand comes from the pipe of previous command line, close write pipe in child process 
            if(FindKeyInNumbered_pipe_table(cmd_number))
            {
                if(input_fd == numbered_pipe_table[cmd_number].input_fd)
                {
                    close(numbered_pipe_table[cmd_number].beginning_write_pipe);
                }
            }
        }

        for(auto iter=user_pipe_table.begin(); iter!=user_pipe_table.end();)
        {
            vector<string> temp = SplitString(iter->first,"->");
            string sender_uid = temp[0];
            string receiver_uid = temp[1];
            sender_uid.erase(0,1);
            receiver_uid.erase(0,1);
            if((stoi(receiver_uid) == uid))
            {
                if(input_fd == iter->second.first)
                {
                    close(iter->second.first);
                    close(iter->second.second);
                    user_pipe_table.erase(iter++);
                    continue;
                }
            }
            iter++;
        }

        if (output_fd != STDOUT_FILENO) 
        {
            if (dup2(output_fd, STDOUT_FILENO) == -1)
                cerr<<cmd_parsed<<" dup error (output_fd)\n";
        }

        if (error_fd != STDERR_FILENO) 
        {
            if (dup2(error_fd, STDERR_FILENO) == -1)
                cerr<<cmd_parsed<<" dup error (error_fd))\n";
        }

        for (size_t i = 0; i < pipe_count; ++i) 
        {
            close(pipe_list[i][0]);
            close(pipe_list[i][1]);
        }

        //file redirection after last command
        if(file_redirection != "" && output_fd == STDOUT_FILENO)
        {
            output_fd = open(file_redirection.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); //write only, create file if it not exist, overwrite
            if (dup2(output_fd, STDOUT_FILENO) == -1)
                cerr<<"dup error (output to created file)\n";
            close(output_fd);
        }

        //parse the cmd that can compatible with "execvp"
        vector<string> after_split = SplitString(cmd_parsed," ");
        char *arg[after_split.size() + 1];
        for (auto i = 0; i < after_split.size(); ++i)
            arg[i] = strdup(after_split[i].c_str()); //use strdup can convert const char* to char*
        arg[after_split.size()] = NULL; //execvp need one more element in array, so put NULL in last position.
        execvp(arg[0], arg);
        cerr << "Unknown command: [" << arg[0] << "]." << "\n";
        exit(EXIT_FAILURE);
    }

    else //parent process
    {
        return child_pid; //parent process will return the process id of child 
    }
    

}

struct sockaddr_in server_addr;
struct sockaddr_in client_addr;
map<int,int> socket_uid_table;
map<int,Client> uid_client_table;

//store the output from use_pipe, the format of the key is "#<sender_uid>->#<receiver_uid>"
//e.g., user1 send user2 "Hello", user_pipe_message_table["#1->#2"].front() = "Hello"
map<string,vector<string>> user_pipe_message_table;

class Chat
{
    public:
        static void SendMessage(int client_socket, string message){
            send(client_socket,message.c_str(),message.length(),0);
        }

        static void BroadcastMessage(string message){
            for(const auto &i:uid_client_table)
            {
                send(i.second.socket_fd,message.c_str(),message.length(),0);
            }
        }

        static void Rename(int uid, string new_name){
            bool name_existed = false;
            for(const auto &i:uid_client_table)
            {
                if(i.second.user_name == new_name)
                {
                    name_existed = true;
                    break;
                }
            }
            if(name_existed)
                cout << "*** User '" << new_name << "' already exists. ***" << endl;
            else
            {
                uid_client_table[uid].user_name = new_name;
                string rename_message = "*** User from " + uid_client_table[uid].ip + ":" + to_string(uid_client_table[uid].port) + " is named '" + new_name + "'. ***\n";
                Chat::BroadcastMessage(rename_message);
            }
        }

        static void Who(int self_uid){
            cout <<"<ID>"<<"\t"<<"<nickname>"<<"\t"<<"<IP:port>"<<"\t"<<"<indicate me>"<<endl;
            for(const auto &i:uid_client_table)
            {
                cout<<i.first << "\t" << i.second.user_name << "\t" << i.second.ip << ":" + to_string(i.second.port);
                if(i.first == self_uid)
                    cout<< "\t<-me";
                cout<<endl;
            }
        }

        static void Tell(int sender, int receiver, string message){
            if(uid_client_table.find(receiver) != uid_client_table.end())
            {
                message = "*** " + uid_client_table[sender].user_name + " told you ***: " + message + "\n";
                Chat::SendMessage(uid_client_table[receiver].socket_fd,message);
            }
            else
                cout << "*** Error: user #" << receiver << " does not exist yet. ***" << endl;
        }

    private:
        Chat() = default;
};

//parse the command from client input to check it need user_pipe or not.
string UserPipeHandler::ParseCommand(int caller_uid, int &input_fd, int &output_fd, string original_command)
{
    vector<string> tokens = SplitString(command," ");

    //handle the client read message from user pipe
    for(auto token: tokens)
    {
        if(token.front() == '<' && token.size() > 1)
        {
            read_pipe_sender_uid = stoi(token.substr(1));
            read_pipe_receiver_uid = caller_uid;
            //the sender doesn't exist
            if(uid_client_table.find(read_pipe_sender_uid) == uid_client_table.end())
            {
                read_from_null = true;
                int dev_null = open("/dev/null",O_RDONLY); //only read from /dev/null
                input_fd = dev_null;
                string message = "*** Error: user #" + to_string(read_pipe_sender_uid) + " does not exist yet. ***";
                cout<<message<<endl;
                command.erase(command.find(token),token.size());
                break;
            }
            //the sender exist but the pipe dose not exist yet
            if(user_pipe_table.find(get_read_pipe_key()) == user_pipe_table.end())
            {
                read_from_null = true;
                int dev_null = open("/dev/null",O_RDONLY); //only read from /dev/null
                input_fd = dev_null;
                string message = "*** Error: the pipe " + get_read_pipe_key() + " does not exist yet. ***";
                cout<<message<<endl;
            }
            //the sender exist and the user pipe also exists
            else
            {
                input_fd = user_pipe_table[get_read_pipe_key()].first;
                string msg = "*** " + uid_client_table[read_pipe_receiver_uid].user_name + " (#" + to_string(read_pipe_receiver_uid) + ") just received from " +
                                uid_client_table[read_pipe_sender_uid].user_name + " (#" + to_string(read_pipe_sender_uid) + ") by '" + original_command + "' ***\n";
                Chat::BroadcastMessage(msg);
            }

            command.erase(command.find(token),token.size());
            break;      
        }       
    }

    //handle the client read message from user pipe
    for(auto token: tokens)
    {
        if(token.front() == '>' && token.size() > 1)
        {
            write_pipe_sender_uid = caller_uid;
            write_pipe_receiver_uid = stoi(token.substr(1));
            //the receiver doesn't exist
            if(uid_client_table.find(write_pipe_receiver_uid) == uid_client_table.end())
            {
                write_to_null = true;
                int dev_null = open("/dev/null",O_WRONLY); // only write to /dev/null
                output_fd = dev_null;
                string message = "*** Error: user #" + to_string(write_pipe_receiver_uid) + " does not exist yet. ***";
                cout<<message<<endl;
                command.erase(command.find(token),token.size());
                break;
            }
            //the receiver exist but the pipe already exists
            if(user_pipe_table.find(get_write_pipe_key()) != user_pipe_table.end())
            {
                write_to_null = true;
                int dev_null = open("/dev/null",O_WRONLY); // only write to /dev/null
                output_fd = dev_null;
                string message = "*** Error: the pipe " + get_write_pipe_key() + " already exists. ***";
                cout<<message<<endl;
            }
            //the receiver exist and the pipe does not exist
            else
            {
                int user_pipe[2];
                pipe(user_pipe);
                fcntl(user_pipe[0],F_SETPIPE_SZ,MAX_PIPE_SIZE);
                user_pipe_table[get_write_pipe_key()] = make_pair(user_pipe[0],user_pipe[1]);
                output_fd = user_pipe[1];
                string msg = "*** " + uid_client_table[write_pipe_sender_uid].user_name + " (#" + to_string(write_pipe_sender_uid) + ") just piped '" + original_command +
                    "' to " + uid_client_table[write_pipe_receiver_uid].user_name + " (#" + to_string(write_pipe_receiver_uid) + ") ***\n";
                Chat::BroadcastMessage(msg);

            }

            command.erase(command.find(token),token.size());
            break;      
        }
    }
    return command;
}

//if the client exit, return true, else return false
bool ReceiveClientMessage(int client_socket)
{
    int uid = socket_uid_table[client_socket];
    bool exit = false;

    //each client has his/her own environment variable, so clear environment before set the environment.
    clearenv(); 
    for(auto i:uid_client_table[uid].env_table)
        setenv(i.first.c_str(),i.second.c_str(),1);

    uid_client_table[uid].BackUp_std_fd();

    dup2(client_socket, STDIN_FILENO);
    dup2(client_socket, STDOUT_FILENO);
    dup2(client_socket, STDERR_FILENO);

    string input_string;
    getline(cin,input_string);
    TrimString(input_string);

    if(input_string.length() == 0)
        exit = false;
    else
    {
        vector<string> after_split = SplitString(input_string," ");
        if(after_split[0] == "printenv")
        {
            char *result = getenv(after_split[1].c_str()); // getenv might return a NULL
            if (result)
                cout << result << "\n";
            uid_client_table[uid].cmd_number++;
        }
        else if(after_split[0] == "setenv")
        {
            uid_client_table[uid].env_table[after_split[1]] = after_split[2];
            uid_client_table[uid].cmd_number++;
        }
        else if(after_split[0] == "exit")
        {
            exit = true;
        }
        else if(after_split[0] == "name")
        {
            Chat::Rename(uid,after_split[1]);
            uid_client_table[uid].cmd_number++;
        }
        else if(after_split[0] == "who")
        {
            Chat::Who(uid);
            uid_client_table[uid].cmd_number++;
        }
        else if(after_split[0] == "tell")
        {
            string message = input_string.substr(input_string.find(after_split[2]));
            Chat::Tell(uid,stoi(after_split[1]),message);
            uid_client_table[uid].cmd_number++;
        }
        else if(after_split[0] == "yell")
        {
            string message = input_string.substr(input_string.find(after_split[1]));
            string prefix_message = "*** " + uid_client_table[uid].user_name + " yelled ***: ";
            Chat::BroadcastMessage(prefix_message+message+"\n");
            uid_client_table[uid].cmd_number++;
        }
        else
        {
            uid_client_table[uid].ExecuteCmd(input_string);
            uid_client_table[uid].cmd_number++;
        }
    }
    
    uid_client_table[uid].restore_std_fd();
    if(exit)
        return true;
    else
        return false;

}

//Clear all the information about the exited user
void ClearUserInformation(int fd, int uid)
{
    socket_uid_table.erase(fd);
    uid_client_table.erase(uid);

    //clear the user_pipe message if it existed
    for(auto iter=user_pipe_table.begin(); iter!=user_pipe_table.end();)
    {
        vector<string> temp = SplitString(iter->first,"->");
        string sender_uid = temp[0];
        string receiver_uid = temp[1];
        sender_uid.erase(0,1);
        receiver_uid.erase(0,1);
        if((stoi(receiver_uid) == uid) || (stoi(sender_uid) == uid))
        {
            close(iter->second.first);
            close(iter->second.second);
            user_pipe_table.erase(iter++);
            continue;
        }
        iter++;
    }
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

    int srv_sock = socket(AF_INET, SOCK_STREAM, 0); //create socket
    if(srv_sock < 0)
    {
        cerr<<"create server error"<<endl;
        exit(1);
    }
        
    bzero((char*)&server_addr,sizeof(server_addr)); //reset server's data to 0
    server_addr.sin_family= AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    server_addr.sin_port = htons(port);

    int option = 1;
    setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    //bind IP & port to socket
    if(bind(srv_sock,(sockaddr*)&server_addr, sizeof(server_addr))<0)
    {
        perror("bind error");
        exit(1);
    }

    //get host name and show host(server) IP
    char host_name[128];
    struct hostent *hent;
    gethostname(host_name, sizeof(host_name));
    hent = gethostbyname(host_name);
    cout<<"The server's IP is "<<inet_ntoa(*(struct in_addr*)(hent->h_addr_list[0]))<<"  port is "<<port<<endl;
    
    if(listen(srv_sock,MAX_CLIENT_NUMBER+10)<0)
    {
        perror("listen error");
        exit(1);
    }


    fd_set read_fds;
    fd_set temp_read_fds; //the read_fds will update after calling select, so we need use a temp_read_fds to select from connected socket.
    FD_ZERO(&read_fds);
    FD_ZERO(&temp_read_fds);
    FD_SET(srv_sock,&read_fds);
    int max_fd = srv_sock; //trace the maximum fd for select
    while(1)
    {
        temp_read_fds = read_fds; // copy read_fds to temp_read_fds before each select
        if(select(max_fd+1,&temp_read_fds,nullptr,nullptr,nullptr) == -1)
        {
            cerr<<"select error"<<endl;
            exit(1);
        }

        for(int fd = 0; fd <= max_fd; fd++)
        {
            if(FD_ISSET(fd,&temp_read_fds))
            {
                if(fd == srv_sock) //server receive new client's connection
                {
                    //accept client's connection
                    socklen_t client_addr_size = sizeof(client_addr);
                    int cli_sock = accept(srv_sock,(sockaddr*)&client_addr,&client_addr_size);
                    cout<<"A client "<<inet_ntoa(client_addr.sin_addr)<<" has connected via port num "<<ntohs(client_addr.sin_port)<<" with socket_fd: "<<cli_sock<<endl;
                    
                    FD_SET(cli_sock,&read_fds); //add client socket to read_fd
                    max_fd = max(max_fd,cli_sock); // update max_fd

                    //find the minimun uid that the new client can use
                    int available_uid = 1;
                    for(int i = 1; i<=MAX_CLIENT_NUMBER; i++)
                    {
                        if(uid_client_table.find(i) == uid_client_table.end())
                        {
                            available_uid = i;
                            break;
                        }
                    }

                    Client client(cli_sock,available_uid,inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port),"(no name)");
                    socket_uid_table[cli_sock] = available_uid;
                    uid_client_table[available_uid] = client;

                    string welcome_message = "****************************************\n"
                                             "** Welcome to the information server. **\n"
                                             "****************************************\n";
                    Chat::SendMessage(cli_sock,welcome_message);
                    
                    string login_message = "*** User '" + client.user_name + "' entered from " + client.ip + ":" + to_string(client.port) +  ". ***\n";
                    Chat::BroadcastMessage(login_message);
                    Chat::SendMessage(cli_sock,"% ");
                }
                else //receive message from online client
                {
                    if(ReceiveClientMessage(fd))
                    {
                        int uid = socket_uid_table[fd];
                        string name = uid_client_table[uid].user_name;
                        //clear user's information
                        ClearUserInformation(fd,uid);

                        string logout_message = "*** User '" + name + "' left. ***\n";
                        Chat::BroadcastMessage(logout_message);
                        close(fd);
                        FD_CLR(fd,&read_fds); 

                    }
                    else
                        Chat::SendMessage(fd,"% ");
                }
            }
        }

    } // END of server select loop
}// END of main function