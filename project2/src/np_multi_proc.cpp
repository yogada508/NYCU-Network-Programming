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
#include <sys/mman.h> //mmap
#include <dirent.h> //directory streams.
#include <algorithm>

using namespace std;

#define MAX_PIPE_NUM 99
#define MAX_USER_NUMBER 30
#define MAX_MESSAGE_LENGTH 1100
#define MAX_PIPE_SIZE 1024*1024
#define IP_LENGTH 30
#define NAME_LENGTH 50

struct sockaddr_in server_addr;
struct sockaddr_in client_addr;

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

struct ClientInfo
{
    int uid = 0;
    int pid = 0;
    uint16_t port = 0;
    char name[NAME_LENGTH] = "";
    char ip[IP_LENGTH] = "";
    char msg_buffer[MAX_MESSAGE_LENGTH] = "";
};

class ClientShell;

//map<int, ClientInfo*> shm_client_table;

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
        UserPipeHandler(string command):temp_read_fd(-1), temp_write_fd(-1),command(command){}
        string ParseCommand(int caller_uid, int &input_fd, int &output_fd, string original_cmd, map<string,int> &fifo_table);
        void CloseTempPipe(){
            if(temp_read_fd != -1)
            {
                close(temp_read_fd);
                close(temp_write_fd);
            }
        }
        void ReadFIFOAndWrite(int fifo_fd, int pipe_fd);

    private:
        string command;
        int temp_read_fd;
        int temp_write_fd;
};

class ClientShell
{
    public:
        ClientShell(string ip, uint16_t port);
        void ExecuteCmd(const string &cmd);
        void loop();
        void CreateFIFO();
        void RemoveFIFO();

        int uid;
        map<string,int> user_pipe_table;
        
    private:

        static void ChildHandler(int signo){ //prevent zombie process
            while (waitpid(-1, NULL, WNOHANG) > 0);
            }

        void inline ClearPipeList(){
            for(size_t i = 0; i<MAX_PIPE_NUM; i++) {pipe_list[i][0]=0; pipe_list[i][1]=0;}
            }

        bool inline FindKeyInNumbered_pipe_table(int key){        
            if (numbered_pipe_table.find(key) != numbered_pipe_table.end()) return true;
            else return false;
            }

        CommandInfo GetCommandInfo(const string &cmd);
        pid_t ForkAndExecute(string cmd, int pipe_count, int input_fd, int output_fd, int error_fd, string file_redirection,UserPipeHandler handler);


        int cmd_number;
        map<int,NumpipeInfo> numbered_pipe_table;
        int pipe_list[MAX_PIPE_NUM][2];
};

class SharedMemory
{
    public:
        map<int, ClientInfo*> shm_client_table;
        int shm_id = 0;
        string name = "";

        //we have to set the signal_handler as static member function, but we also want to access non static member in handler
        //so we define a function can return a static object, so that we can use this object in static function.
        static SharedMemory& get_instance(){
            static SharedMemory instance;
            return instance;
        }

        //create the number of maximum clients pointer to shared memory
        void CreateAllClients(){
            ClientInfo *shm_client;
            for(int i = 1; i<=MAX_USER_NUMBER; i++)
            {
                shm_client = (ClientInfo*)mmap(NULL, sizeof(ClientInfo), PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
                if (shm_client == MAP_FAILED)
                    cerr<<"mmap error"<<endl;
                shm_client_table[i] = shm_client;
            }
        }

        void FreeSharedMemory(){
            for(int i = 1; i<=MAX_USER_NUMBER; i++)
                munmap(shm_client_table[i],sizeof(ClientInfo));
        }
        
        //return the available uid that the new user can use.
        int AddUser(int pid, string ip, uint16_t port){
            for(int i = 1; i<= MAX_USER_NUMBER; i++)
            {
                if(shm_client_table[i]->uid == 0) //find the minimum available user id
                {
                    shm_client_table[i]->uid = i;
                    shm_client_table[i]->pid = pid;
                    strcpy(shm_client_table[i]->ip, ip.c_str());
                    strcpy(shm_client_table[i]->name, "(no name)");
                    shm_client_table[i]->port = port;
                    shm_id = i;
                    name = "(no name)";
                    return i;
                }
            }
            return 0;
        }

        void RemoveUser(int uid){
            memset(shm_client_table[uid]->ip,0,IP_LENGTH);
            memset(shm_client_table[uid]->msg_buffer,0,MAX_MESSAGE_LENGTH);
            memset(shm_client_table[uid]->name,0,NAME_LENGTH);
            shm_client_table[uid]->pid = 0;
            shm_client_table[uid]->port = 0;
            shm_client_table[uid]->uid = 0;

        }

        void BroadcastMessage(string message){
            for(int i = 1; i<= MAX_USER_NUMBER; i++)
            {
                if(shm_client_table[i]->uid != 0)
                {
                    strcpy(shm_client_table[i]->msg_buffer, message.c_str());
                    kill(shm_client_table[i]->pid, SIGUSR1);
                }
            }
        }

        void Rename(int uid, string new_name){
            bool name_existed = false;
            for(int i = 1; i<= MAX_USER_NUMBER; i++)
            {
                if(string(shm_client_table[i]->name) == new_name)
                {
                    name_existed = true;
                    break;
                }
            }
            if(name_existed)
                cout << "*** User '" << new_name << "' already exists. ***" << endl;
            else
            {
                strcpy(shm_client_table[uid]->name, new_name.c_str());
                name = new_name;
                string rename_message = "*** User from " + string(shm_client_table[uid]->ip) + ":" 
                                        + to_string(shm_client_table[uid]->port) + " is named '" 
                                        + new_name + "'. ***\n";
                BroadcastMessage(rename_message);
            }
        }

        void Who(int self_uid){
            cout <<"<ID>"<<"\t"<<"<nickname>"<<"\t"<<"<IP:port>"<<"\t"<<"<indicate me>"<<endl;
            for(int i = 1; i<= MAX_USER_NUMBER; i++)
            {
                if(shm_client_table[i]->uid != 0)
                {
                    cout<<i << "\t" << string(shm_client_table[i]->name) << "\t"
                        << string(shm_client_table[i]->ip) << ":" + to_string(shm_client_table[i]->port);
                    if(i == self_uid)
                        cout<< "\t<-me";
                    cout<<endl;
                }

            }           
        }

        void Tell(int sender, int receiver, string message){
            if(shm_client_table[receiver]->uid == 0)
                cout << "*** Error: user #" << receiver << " does not exist yet. ***" << endl;
            else
            {
                message = "*** " + string(shm_client_table[sender]->name) + " told you ***: " + message + "\n";
                strcpy(shm_client_table[receiver]->msg_buffer, message.c_str());
                kill(shm_client_table[receiver]->pid, SIGUSR1);
            }
        }


    private:
        SharedMemory(){signal(SIGUSR1, shm_SignalHandler);}

        static void shm_SignalHandler(int signo){
            if(signo == SIGUSR1)
            {
                int id = get_instance().shm_id;
                auto client_info = get_instance().shm_client_table[id];
                cout<<client_info->msg_buffer;
                cout.flush();
                memset(client_info->msg_buffer, 0, MAX_MESSAGE_LENGTH);

            }
        }

};

static void main_SignalHandler(int signal_number)
{
    if (signal_number == SIGCHLD)  //prevent zombie process
        while (waitpid(-1, NULL, WNOHANG) > 0);

    if (signal_number == SIGINT || signal_number == SIGQUIT || signal_number == SIGTERM) {
        // free shared memory
        SharedMemory::get_instance().FreeSharedMemory();

        //delete all FIFO file created by client
        DIR *d;
        struct dirent *dir;
        d = opendir("./user_pipe");
        if (d)
        {
            while ((dir = readdir(d)) != NULL)
            {
                string file_name = "./user_pipe/" + string(dir->d_name);
                unlink(file_name.c_str());
            }
            closedir(d);
        }
        //exit the server process
        exit(0);
    }
    
}

int main(int argc, char *argv[])
{
    signal(SIGCHLD, main_SignalHandler);
    signal(SIGINT, main_SignalHandler);
    signal(SIGQUIT, main_SignalHandler);
    signal(SIGTERM, main_SignalHandler);

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
    
    if(listen(srv_sock,5)<0)
    {
        perror("listen error");
        exit(1);
    }

    //create shared memory for all users and put in the shm_client_table
    /*ClientInfo *shm_client;
    for(int i = 1; i<=MAX_USER_NUMBER; i++)
    {
        shm_client = (ClientInfo*)mmap(NULL, sizeof(ClientInfo), PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
        if (shm_client == MAP_FAILED)
            cerr<<"mmap error"<<endl;
        shm_client_table[i] = shm_client;
    }*/
    SharedMemory::get_instance().CreateAllClients();

    while(1)
    {
        //accept client's connection
        socklen_t client_addr_size = sizeof(client_addr);
        int cli_sock = accept(srv_sock,(sockaddr*)&client_addr,&client_addr_size);
        cout<<"A client "<<inet_ntoa(client_addr.sin_addr)<<" has connected via port num "<<ntohs(client_addr.sin_port)<<" with socket_fd: "<<cli_sock<<endl;

        pid_t pid = fork();
        if(pid == 0) //child process
        {
            dup2(cli_sock, STDIN_FILENO);
            dup2(cli_sock, STDOUT_FILENO);
            dup2(cli_sock, STDERR_FILENO);
            ClientShell client_shell(inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));
            client_shell.loop();

            //the client exit.
            client_shell.RemoveFIFO();
            string name = SharedMemory::get_instance().name;
            SharedMemory::get_instance().RemoveUser(client_shell.uid);
            string logout_message = "*** User '" + name + "' left. ***\n";
            SharedMemory::get_instance().BroadcastMessage(logout_message);
            close(cli_sock);
            close(srv_sock);
            exit(EXIT_SUCCESS);
        }
        else // parent process will wait it's child
        {
            close(cli_sock);
        }
    }
}

ClientShell::ClientShell(string ip, uint16_t port)
{
    //signal(SIGUSR1, static_SignalHandler);
    cmd_number = 1;
    clearenv();
    setenv("PATH","bin:.",1);
    uid = SharedMemory::get_instance().AddUser(getpid(),ip,port);
    //cout<<"my id: "<<uid<<endl;

    string welcome_message = "****************************************\n"
                             "** Welcome to the information server. **\n"
                             "****************************************\n";
    cout<<welcome_message;
    string login_message = "*** User '" + string(SharedMemory::get_instance().shm_client_table[uid]->name) + "' entered from " 
                            + string(SharedMemory::get_instance().shm_client_table[uid]->ip) + ":" 
                            + to_string(SharedMemory::get_instance().shm_client_table[uid]->port) +  ". ***\n";
    SharedMemory::get_instance().BroadcastMessage(login_message);

}

void ClientShell::loop()
{
    while (1)
    {
        cout<<"% ";
        string input_cmd;
        getline(cin,input_cmd);

        TrimString(input_cmd); //trim the string before split it
        if(input_cmd.length() == 0)
        {
            continue;
        }
        else
        {
            vector<string> after_split = SplitString(input_cmd," ");
            if(after_split[0] == "printenv")
            {
                char *result = getenv(after_split[1].c_str()); // getenv might return a NULL
                if (result)
                    cout << result << "\n";
                cmd_number++;
            }
            else if(after_split[0] == "setenv")
            {
                setenv(after_split[1].c_str(), after_split[2].c_str(), 1);
                cmd_number++;
            }
            else if(after_split[0] == "exit")
                break;
            
            else if(after_split[0] == "name")
            {
                SharedMemory::get_instance().Rename(uid,after_split[1]);
                cmd_number++;
            }
            else if(after_split[0] == "who")
            {
                SharedMemory::get_instance().Who(uid);
                cmd_number++;
            }
            else if(after_split[0] == "tell")
            {
                string message = input_cmd.substr(input_cmd.find(after_split[2]));
                SharedMemory::get_instance().Tell(uid,stoi(after_split[1]),message);
                cmd_number++;
            }
            else if(after_split[0] == "yell")
            {
                string message = input_cmd.substr(input_cmd.find(after_split[1]));
                string prefix_message = "*** " + SharedMemory::get_instance().name + " yelled ***: ";
                SharedMemory::get_instance().BroadcastMessage(prefix_message+message+"\n");
                cmd_number++;
            }
            else
            {
                ExecuteCmd(input_cmd);
                cmd_number++;
            }

        }
    }

}

void ClientShell::ExecuteCmd(const string &cmd)
{
    signal(SIGCHLD,ChildHandler);
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
            string cmd_parsed = handler.ParseCommand(uid,input_fd,output_fd,cmd,this->user_pipe_table);

            pid_t child_pid = ForkAndExecute(cmd_parsed,command_batch_pipe_cnt,input_fd,output_fd,error_fd,command_info.file_redirection,handler);
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
            handler.CloseTempPipe();
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

CommandInfo ClientShell::GetCommandInfo(const string &cmd)
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

pid_t ClientShell::ForkAndExecute(string cmd_parsed, int pipe_count, int input_fd, int output_fd, int error_fd, string file_redirection, UserPipeHandler handler)
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
        handler.CloseTempPipe();

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

//remove all fifo related to the exited client
void ClientShell::RemoveFIFO()
{
    DIR *d;
    struct dirent *dir;
    d = opendir("./user_pipe");
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            string fifo_name = string(dir->d_name);
            vector<string> temp = SplitString(fifo_name,"_");
            string sender_uid, receiver_uid;
            if(temp.size()>1)
            {
                sender_uid = temp[0];
                receiver_uid = temp[1];
                if((stoi(receiver_uid) == uid) || (stoi(sender_uid) == uid))
                {
                    string file_name = "./user_pipe/" + fifo_name;
                    unlink(file_name.c_str());
                }
            }
        }
        closedir(d);
    }
}

string UserPipeHandler::ParseCommand(int caller_uid, int &input_fd, int &output_fd, string original_cmd, map<string,int> &fifo_table)
{
    vector<string> tokens = SplitString(command," ");

    //check the created fifo still exists or not
    for(auto iter=fifo_table.begin(); iter!=fifo_table.end();)
    {
        string fifo_name = iter->first; 
        if(access(fifo_name.c_str(), F_OK) != 0) //fifo doesn't exist (is already read from other client)
        {
            close(iter->second);
            fifo_table.erase(iter++);
            continue;
        }
        iter++;
    }

    //handle the client read message from FIFO
    for(auto token: tokens)
    {
        if(token.front() == '<' && token.size() > 1)
        {
            int sender_uid = stoi(token.substr(1));
            int receiver_uid = caller_uid;

            //the sender doesn't exist
            if(SharedMemory::get_instance().shm_client_table[sender_uid]->uid == 0)
            {
                int dev_null = open("/dev/null",O_RDONLY); // only write to /dev/null
                input_fd = dev_null;
                string message = "*** Error: user #" + to_string(sender_uid) + " does not exist yet. ***";
                cout<<message<<endl;
                command.erase(command.find(token),token.size());
                break;
            }
            //the sender exist but the pipe dose not exist yet
            string fifo_name = "./user_pipe/" + to_string(sender_uid) + "_" + to_string(receiver_uid);
            if(access(fifo_name.c_str(), F_OK) != 0)
            {
                int dev_null = open("/dev/null",O_RDONLY); // only write to /dev/null
                input_fd = dev_null;
                string message = "*** Error: the pipe #" + to_string(sender_uid) + "->#" + to_string(receiver_uid) + " does not exist yet. ***";
                cout<<message<<endl;                
            }
            //the sender exist and the user pipe also exists
            else
            {
                int read_fifo_fd = open(fifo_name.c_str(), O_RDONLY | O_NONBLOCK);
                //create a pipe and write the message which read from FIFO to this pipe
                int temp_pipe[2];
                pipe(temp_pipe);
                temp_read_fd = temp_pipe[0];
                temp_write_fd = temp_pipe[1];
                fcntl(temp_write_fd,F_SETPIPE_SZ,MAX_PIPE_SIZE);
                input_fd = temp_pipe[0];

                //read from FIFO
                ReadFIFOAndWrite(read_fifo_fd, temp_write_fd);
                close(read_fifo_fd);
                unlink(fifo_name.c_str());

                //broadcast
                string sender_name = SharedMemory::get_instance().shm_client_table[sender_uid]->name;
                string receiver_name = SharedMemory::get_instance().shm_client_table[receiver_uid]->name;
                string msg = "*** " + receiver_name + " (#" + to_string(receiver_uid) + ") just received from " +
                             sender_name + " (#" + to_string(sender_uid) + ") by '" + original_cmd + "' ***\n";
                SharedMemory::get_instance().BroadcastMessage(msg);
            }

            command.erase(command.find(token),token.size());
            break;
        }
    }
    
    //handle the client write message to FIFO
    for(auto token: tokens)
    {
        if(token.front() == '>' && token.size() > 1)
        {
            int sender_uid = caller_uid;
            int receiver_uid = stoi(token.substr(1));

            //the receiver doesn't exist
            if(SharedMemory::get_instance().shm_client_table[receiver_uid]->uid == 0)
            {
                int dev_null = open("/dev/null",O_WRONLY); // only write to /dev/null
                output_fd = dev_null;
                string message = "*** Error: user #" + to_string(receiver_uid) + " does not exist yet. ***";
                cout<<message<<endl;
                command.erase(command.find(token),token.size());
                break;
            }
            //the receiver exist but the pipe already exists
            string fifo_name = "./user_pipe/" + to_string(sender_uid) + "_" + to_string(receiver_uid);
            if(access(fifo_name.c_str(), F_OK) == 0)
            {
                int dev_null = open("/dev/null",O_WRONLY); // only write to /dev/null
                output_fd = dev_null;
                string message = "*** Error: the pipe #" + to_string(sender_uid) + "->#" + to_string(receiver_uid) + " already exists. ***";
                cout<<message<<endl;
            }
            //the receiver exist and the pipe does not exist
            else
            {
                int fifo = mkfifo(fifo_name.c_str(), 0644);
                int write_fd = open(fifo_name.c_str(), O_RDWR | O_NONBLOCK);
                fcntl(write_fd,F_SETPIPE_SZ,MAX_PIPE_SIZE);
                fifo_table[fifo_name] = write_fd;
                output_fd = write_fd;

                string sender_name = SharedMemory::get_instance().shm_client_table[sender_uid]->name;
                string receiver_name = SharedMemory::get_instance().shm_client_table[receiver_uid]->name;
                string msg = "*** " + sender_name + " (#" + to_string(sender_uid) + ") just piped '" + original_cmd +
                    "' to " + receiver_name + " (#" + to_string(receiver_uid) + ") ***\n";
                SharedMemory::get_instance().BroadcastMessage(msg);
            }

            command.erase(command.find(token),token.size());
            break;
        }
    }

    return command;
}

void UserPipeHandler::ReadFIFOAndWrite(int fifo_fd, int pipe_fd)
{
    char read_buffer[1025]; //one more space for '\0' due to strlen() only works if a null terminator '\0' is present in the array of characters.
    while(1)
    {
        memset(read_buffer, 0, sizeof(read_buffer)); //clear buffer before read data from pipe
        int read_len = read(fifo_fd,read_buffer,sizeof(read_buffer)-1); //only read sizeof()-1 bytes
        read_buffer[sizeof(read_buffer)-1] = '\0'; //the last character is '\0' to end the string.

        if(read_len == 0 || read_len == -1) //read end from the FIFO
        {
            break;
        }
        else
        {
            write(pipe_fd,read_buffer,strlen(read_buffer));
        }
    }
}