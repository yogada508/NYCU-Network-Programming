#include <iostream>
#include <vector>
#include <string>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <iterator>
#include <sstream>
#include <map>
#include <algorithm>

using namespace std;

#define MAX_PIPE_NUM 99
#define MAX_PIPE_SIZE 1024*1024 //1M for pipe size

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

class MyShell
{
    public:
        MyShell(){cmd_number = 1; setenv("PATH","bin:.",1);}; //initial PATH is bin/ and ./
        void ExecuteCmd(const string &cmd);
        void loop();

        
    private:
        static void ChildHandler(int signo){
            int status; 
            while (waitpid(-1, &status, WNOHANG) > 0) {}
            }; //prevent zombie process
        
        void inline ClearPipeList(){
            for(size_t i = 0; i<MAX_PIPE_NUM; i++) {pipe_list[i][0]=0; pipe_list[i][1]=0;}};

        bool inline FindKeyInMap(int key){        
            if (numbered_pipe_table.find(key) != numbered_pipe_table.end()) return true;
            else return false;};

        void TrimString(string &input);
        vector<string> SplitString(const string &input, string delimiter);
        CommandInfo GetCommandInfo(const string &cmd);
        pid_t ForkAndExecute(string cmd, int pipe_count, int input_fd, int output_fd, int error_fd, string file_redirection);
        void ForkAndStore(int cmd_number, int read_from_parent, int write_to_store, int read_from_store, int write_to_child);
        void UnsetExecutedCommandState(int target_fd);

        int cmd_number; //record the command number from input. (include unknown command, but not include empty input)
        map<int,NumpipeInfo> numbered_pipe_table;
        int pipe_list[MAX_PIPE_NUM][2];

};

//remove leading and trailing spaces in string
void MyShell::TrimString(string &input)
{
    input.erase(input.find_last_not_of(" ")+1);
    input.erase(0,input.find_first_not_of(" "));
}

//split the string by the delimiter
vector<string> MyShell::SplitString(const string &input, string delimiter)
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

void MyShell::loop()
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
            else
            {
                ExecuteCmd(input_cmd);
                cmd_number++;
            }

        }
    }
}

void MyShell::ExecuteCmd(const string &cmd)
{
    signal(SIGCHLD,ChildHandler); //the child will send SIGCHLD signal to parent after it exit, so use ChildHandler to handle this signal1.
    CommandInfo command_info = GetCommandInfo(cmd);
    ClearPipeList();

    vector<pid_t> pid_list;

    //create numbered pipe if this input command need it.
    if(command_info.numbered_pipe > 0)
    {
        //if the numbered pipe target doesn't exist, create a new pipe and fork a process to store all messages
        if(!FindKeyInMap(cmd_number+command_info.numbered_pipe))
        {
            NumpipeInfo num_pipe_info;
            if(!FindKeyInMap(cmd_number))
                numbered_pipe_table[cmd_number] = num_pipe_info;

            numbered_pipe_table[cmd_number + command_info.numbered_pipe] = num_pipe_info;

            int pipe_to_store[2]; //create two pipe, one for output the message to store_process
            int pipe_from_store[2]; //the other one for output the stored message to numbered pipe command
            pipe(pipe_to_store);
            pipe(pipe_from_store);
   
            //fcntl(pipe_fd[0],F_SETPIPE_SZ,MAX_PIPE_SIZE);

            numbered_pipe_table[cmd_number].output_fd = pipe_to_store[1];
            numbered_pipe_table[cmd_number + command_info.numbered_pipe].input_fd = pipe_from_store[0];
            numbered_pipe_table[cmd_number + command_info.numbered_pipe].beginning_write_pipe = pipe_to_store[1];
            if(command_info.pipe_stderr)
                numbered_pipe_table[cmd_number].error_fd = pipe_to_store[1];
            
            ForkAndStore(cmd_number,pipe_to_store[0],pipe_to_store[1],pipe_from_store[0],pipe_from_store[1]);
        }
        //if the numbered pipe target exist, use the existed pipe
        else
        {
            NumpipeInfo num_pipe_info;
            if(!FindKeyInMap(cmd_number))
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
        if(!FindKeyInMap(cmd_number))
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
        bool cmd_in_map = FindKeyInMap(cmd_number);
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


            pid_t child_pid = ForkAndExecute(command_batch[j],command_batch_pipe_cnt,input_fd,output_fd,error_fd,command_info.file_redirection);
            pid_list.push_back(child_pid);

            //if this command line is piped from previous command line, close the write and read pipe in the parent.
            if(cmd_in_map)
            {
                if(input_fd == numbered_pipe_table[cmd_number].input_fd && input_fd != STDIN_FILENO)
                {
                    close(input_fd);
                    close(numbered_pipe_table[cmd_number].beginning_write_pipe);
                    UnsetExecutedCommandState(numbered_pipe_table[cmd_number].beginning_write_pipe);
                }
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
            //cout<<j<<" exit!\n";
        }
        
    }

}

CommandInfo MyShell::GetCommandInfo(const string &cmd)
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

pid_t MyShell::ForkAndExecute(string cmd, int pipe_count, int input_fd, int output_fd, int error_fd, string file_redirection)
{
    pid_t child_pid;
    while ((child_pid = fork()) < 0) //if fork error (i.e., too many process is running), wait 2ms to fork again
        usleep(2000);

    if(child_pid == 0) //child process
    {
        //cout<<"pid "<<getpid()<<": "<<cmd<<" "<<input_fd<<" "<<output_fd<<" "<<error_fd<<endl;
        if (input_fd != STDIN_FILENO) 
        {
            if (dup2(input_fd, STDIN_FILENO) == -1)
                cerr<<cmd<<" dup error (input_fd)\n";

            //if the input of this subcommand comes from the pipe of previous command line, close write pipe in child process 
            if(FindKeyInMap(cmd_number))
            {
                if(input_fd == numbered_pipe_table[cmd_number].input_fd)
                {
                    close(numbered_pipe_table[cmd_number].beginning_write_pipe);
                }
            }
        }

        if (output_fd != STDOUT_FILENO) 
        {
            if (dup2(output_fd, STDOUT_FILENO) == -1)
                cerr<<cmd<<" dup error (output_fd)\n";
        }

        if (error_fd != STDERR_FILENO) 
        {
            if (dup2(error_fd, STDERR_FILENO) == -1)
                cerr<<cmd<<" dup error (error_fd))\n";
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
        vector<string> after_split = SplitString(cmd," ");
        char *arg[after_split.size() + 1];
        for (auto i = 0; i < after_split.size(); ++i)
            arg[i] = strdup(after_split[i].c_str()); //use strdup can convert const char* to char*
        arg[after_split.size()] = NULL; //execvp need one more element in array, so put NULL in last position.
        execvp(arg[0], arg);
        cerr << "Unknown command: [" << arg[0] << "]." << "\n";
        exit(EXIT_FAILURE);
    }

    return child_pid; //parent process will return the process id of child 

}

//to prevent the size of numbered pipe output exceed the capacity of pipe, fork a process to store all numbered pipe output message
void MyShell::ForkAndStore(int cmd_number, int read_from_parent, int write_to_store, int read_from_store, int write_to_child)
{
    pid_t store_process_pid;

    while ((store_process_pid = fork()) < 0)
        usleep(2000);

    if(store_process_pid == 0) //child process
    {   
        //if the previous store process hasn't exited and then the parent call fork(), 
        //in new store process need to close the output_fd of other processes, so that only the parent process keep the last piece of output_fd 
        for(auto &i:numbered_pipe_table)
        {
            if(i.first <= cmd_number && i.second.output_fd != -1)
            {
                close(i.second.output_fd);
            }
        }

        char read_buffer[1025]; //one more space for '\0' due to strlen() only works if a null terminator '\0' is present in the array of characters.
        char write_buffer[1024];
        vector<string> stored_message;

        //read the message from pipe until the parent close the write side
        while(1)
        {
            memset(read_buffer, 0, sizeof(read_buffer)); //clear buffer before read data from pipe
            int read_len = read(read_from_parent,read_buffer,sizeof(read_buffer)-1); //only read sizeof()-1 bytes
            read_buffer[sizeof(read_buffer)-1] = '\0'; //the last character is '\0' to end the string.

            if(read_len == 0) //the writer close the pipe
            {
                break;
            }
            else
            {
                string temp = read_buffer;
                stored_message.push_back(temp);
            }
        }

        //and then write the message to the numbered pipe target.
        for(const auto &i:stored_message)
        {
            strcpy(write_buffer,i.c_str());
            write(write_to_child,write_buffer,strlen(write_buffer));
        }
        //cout<<"store process exit!\n";
        exit(0);
        
    }
    else //parent process
    {
        close(read_from_parent);
        close(write_to_child);
    }
}

//set the output_fd of executed command to -1 so that the store process does not close the fd has already closed
void MyShell::UnsetExecutedCommandState(int target_fd)
{
    for(auto &i:numbered_pipe_table)
    {
        if(i.second.output_fd == target_fd)
            i.second.output_fd = -1;
    }
}

int main()
{
    MyShell shell;
    shell.loop();    
    return 0;

}