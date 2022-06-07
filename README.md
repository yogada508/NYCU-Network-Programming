# NYCU Network Programming 2021 Fall
## HW0
In this homework, you are given a part of a program and a terminal output of two processes executed simultaneously. You should figure out possible
context switch points according to the output.  

## Project 1: NPshell
In this project, you are asked to design a shell named npshell. The npshell should support the
following features:
1. Execution of commands
2. Ordinary Pipe
3. Numbered Pipe
4. File Redirection

## Project 2: Remote Working Ground (rwg) Server
In this project, you are asked to design 3 kinds of servers:
1. Design a Concurrent connection-oriented server. This server allows one client connect to it.
2. Design a server of the chat-like systems, called remote working systems (rwg). In this system, users can communicate with
other users. You need to use the single-process concurrent paradigm to design this server.
3. Design the rwg server using the concurrent connection-oriented paradigm with shared memory and FIFO.  

These three servers must support all functions in project 1.

## Project 3: HTTP Server & CGI
In this project, you are asked to implement a simple HTTP server called http server and a CGI program
console.cgi. We will use Boost.Asio library to accomplish this project.

## Project 4: SOCKS 4
In this project, you are going to implement the SOCKS 4/4A protocol in the application layer of the OSI model.  
SOCKS is similar to a proxy (i.e., intermediary-program) that acts as both server and client for the purpose of making requests on behalf of other clients. Because the SOCKS protocol is independent of application protocols, it can be used for many different services: telnet, ftp, WWW, etc.  
There are two types of the SOCKS operations, namely CONNECT and BIND. You have to implement both of them in this project. We will use Boost.Asio library to accomplish this project.
