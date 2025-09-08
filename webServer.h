#ifndef HEADER_H
#define HEADER_H 

#include <iostream>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "logging.h"

enum HttpType {
    INVALID = 0, GET, HEAD, POST
};

inline int BUFFER_SIZE = 10;

#endif
