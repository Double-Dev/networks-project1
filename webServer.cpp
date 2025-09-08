// **************************************************************************************
// * webServer (webServer.cpp)
// * - Implements a very limited subset of HTTP/1.0, use -v to enable verbose debugging output.
// * - Port number 1701 is the default, if in use random number is selected.
// *
// * - GET requests are processed, all other metods result in 400.
// *     All header gracefully ignored
// *     Files will only be served from cwd and must have format file\d.html or image\d.jpg
// *
// * - Response to a valid get for a legal filename
// *     status line (i.e., response method)
// *     Cotent-Length:
// *     Content-Type:
// *     \r\n
// *     requested file.
// *
// * - Response to a GET that contains a filename that does not exist or is not allowed
// *     statu line w/code 404 (not found)
// *
// * - CSCI 471 - All other requests return 400
// * - CSCI 598 - HEAD and POST must also be processed.
// *
// * - Program is terminated with SIGINT (ctrl-C)
// **************************************************************************************
#include "webServer.h"

struct RequestData
{
  HttpType type;
  int status;
};

// **************************************************************************************
// * Signal Handler.
// * - Display the signal and exit (returning 0 to OS indicating normal shutdown)
// * - Optional for 471, required for 598
// **************************************************************************************
void sig_handler(int signo) {
  std::cout << "Caught signal: " << strsignal(signo) << std::endl;
  std::cout << "Exiting program..." << std::endl;
  // Close from 5 to allow listen fd and potentially connection & file fd's to close.
  closefrom(5);
  exit(0);
}


// **************************************************************************************
// * processRequest,
//   - Return HTTP code to be sent back
//   - Set filename if appropriate. Filename syntax is valided but existance is not verified.
// **************************************************************************************
RequestData readRequest(int sockFd, std::string &filename) {
  int bytesRead;
  char buffer[BUFFER_SIZE];

  std::stringstream stream;
  std::string headerStr;

  bool foundEnd = false;
  int endIndex = -1;
  while (!foundEnd) {
    bytesRead = read(sockFd, buffer, BUFFER_SIZE);
    if (bytesRead < 0) {
      ERROR << "Couldn't read from connection: " << strerror(errno) << ENDL;
      closefrom(4);
      exit(-1);
    }

    for (int i = 0; i < bytesRead; i++) {
      stream << buffer[i];
      headerStr = stream.str();
      if (headerStr[headerStr.size()-4] == '\r' && headerStr[headerStr.size()-3] == '\n' &&
        headerStr[headerStr.size()-2] == '\r' && headerStr[headerStr.size()-1] == '\n') {
        foundEnd = true;
        endIndex = i;
        break;
      }
    }
  }

  DEBUG << "Read header:\n" << headerStr << ENDL;

  HttpType requestType = HttpType::INVALID;
  if (headerStr.compare(0, 3, "GET") == 0) {
    requestType = HttpType::GET;
  } else if (headerStr.compare(0, 4, "HEAD") == 0) {
    requestType = HttpType::HEAD;
  } else if (headerStr.compare(0, 4, "POST") == 0) {
    requestType = HttpType::POST;
  }

  if (requestType == HttpType::GET || requestType == HttpType::HEAD) {
    // Checking if file matches the required pattern:
    if (std::regex_search(headerStr, std::regex("(GET|HEAD) /(file\\d\\.html|image\\d\\.jpg) (\\r\\n|.)*"))) {
      // Checking if file is in the data directory:
      for (const auto& entry : std::filesystem::directory_iterator("./data")) {
        std::string currentFilename = entry.path().filename().string();
        DEBUG << "Checking: " << headerStr.substr(headerStr.find(' ')+1, currentFilename.size()) << " against: " << currentFilename << ENDL;
        // If the filenames are equal:
        if (headerStr.compare(headerStr.find('/')+1, currentFilename.size(), currentFilename) == 0) {
          filename.assign(currentFilename);
          DEBUG << "Found match: " << filename << ENDL;
          return { requestType, 200 };
        }
      }
    }
    return { requestType, 404 };
  } else if (requestType == HttpType::POST) {
    // Calculating the length of the body:
    std::string pattern = "Content-Length: ";
    std::string lengthStr = headerStr.substr(headerStr.find(pattern));
    lengthStr = lengthStr.substr(0, lengthStr.find("\r\n"));
    lengthStr = lengthStr.substr(lengthStr.find(" "));
    int contentLength = std::stoi(lengthStr);

    // Setting the body buffer to be 1 char longer for a null-termination byte.
    char bodyBuffer[contentLength+1];
    memset(bodyBuffer, 0, contentLength+1);
    // Setting the chars that were already read when processing the header:
    int bufIndex = 0;
    while (endIndex+1 < bytesRead) {
      endIndex++;
      bodyBuffer[bufIndex] = buffer[endIndex];
      bufIndex++;
    }
    // Reading the remaining chars from the socket:
    if (bufIndex < contentLength && read(sockFd, &bodyBuffer[bufIndex], contentLength-bufIndex) < 0) {
      ERROR << "Couldn't read from connection: " << strerror(errno) << ENDL;
      closefrom(4);
      exit(-1);
    }

    // Trimming body string down to just the filename:
    std::string bodyStr = std::string(bodyBuffer);
    DEBUG << "Body Content:\n" << bodyStr << ENDL;
    int eqIndex = bodyStr.find('=');
    // If the '=' can't be found for some reason:
    if (eqIndex == std::string::npos) {
      ERROR << "Invalid filename in body: " << bodyStr << ENDL;
      return { requestType, 400 };
    }
    // Setting the filename:
    filename.assign(bodyStr.substr(eqIndex+1));
    return { requestType, 201 };
  }
  return { requestType, 400 };
}


// **************************************************************************
// * Send one line (including the line terminator <LF><CR>)
// * - Assumes the terminator is not included, so it is appended.
// **************************************************************************
void sendLine(int socketFd, std::string stringToSend) {
  // Create copy of stringToSend with line terminator appended to it: 
  const std::string modifiedToSend = stringToSend + "\r\n";
  // Sending the char buffer through the connection:
  if (write(socketFd, modifiedToSend.c_str(), modifiedToSend.size()) < 0) {
    ERROR << "Couldn't write to connection: " << strerror(errno) << ENDL;
    closefrom(4);
    exit(-1);
  }
  return;
}

// **************************************************************************
// * Send the entire 404 response, header and body.
// **************************************************************************
void send404(int sockFd) {
  sendLine(sockFd, "HTTP/1.0 404 Not Found");
  sendLine(sockFd, "content-type: text/html");
  sendLine(sockFd, "");
  sendLine(sockFd, "File not found.");
  sendLine(sockFd, "");
  return;
}

// **************************************************************************
// * Send the entire 400 response, header and body.
// **************************************************************************
void send400(int sockFd) {
  sendLine(sockFd, "HTTP/1.0 400 Bad Request");
  sendLine(sockFd, "");
  return;
}


// **************************************************************************************
// * send200
// * -- Send the 200 response header.
// * -- Send the file if it's a GET request
// **************************************************************************************
void send200(int sockFd, std::string filename, bool sendFile) {
  std::string path = std::string("./data/") + filename;
  // If file can't be read, send 404.
  struct stat fileInfo;
  int fileFd;
  if (stat(path.c_str(), &fileInfo) < 0 || (fileFd = open(path.c_str(), O_RDONLY)) < 0) {
    send404(sockFd);
    return;
  }

  // Sending the header:
  DEBUG << "Sending the header." << ENDL;
  sendLine(sockFd, "HTTP/1.0 200 OK");
  std::filesystem::path file(filename);
  std::string fileExtension = file.extension().string();
  if (fileExtension.compare(".html") == 0) {
    sendLine(sockFd, "content-type: text/html");
  } else if (fileExtension.compare(".jpg") == 0 || fileExtension.compare(".jpeg") == 0) {
    sendLine(sockFd, "content-type: image/jpeg");
  } else {
    // Default content type if not a supported type.
    sendLine(sockFd, "content-type: application/octet-stream");
  }
  std::string fileSize = std::to_string(fileInfo.st_size);
  DEBUG << "File size: " << fileSize << ENDL;
  sendLine(sockFd, std::string("content-length: ") + fileSize);
  sendLine(sockFd, "");

  if (sendFile) {
    // Sending the file:
    DEBUG << "Sending the file." << ENDL;
    int totalBytesRead = 0;
    char buffer[BUFFER_SIZE];
    while (totalBytesRead < fileInfo.st_size) {
      int bytesRead = read(fileFd, &buffer, BUFFER_SIZE);
      if (bytesRead < 0) {
        ERROR << "Unable to read from file: " << strerror(errno) << ENDL;
        closefrom(5);
        exit(-1);
      }

      if (write(sockFd, &buffer, bytesRead) < 0) {
        ERROR << "Couldn't write to connection: " << strerror(errno) << ENDL;
        closefrom(5);
        exit(-1);
      }

      totalBytesRead += bytesRead;
    }
  }
  close(fileFd);
  return;
}


// **************************************************************************
// * Send the entire 201 response, header and body.
// **************************************************************************
void send201(int sockFd) {
  sendLine(sockFd, "HTTP/1.0 201 Created");
  sendLine(sockFd, "");
  return;
}


// **************************************************************************************
// * processConnection
// * -- process one connection/request.
// **************************************************************************************
int processConnection(int sockFd) {
  // Call readHeader()
  std::string filename = "";
  RequestData requestData = readRequest(sockFd, filename);

  switch (requestData.status)
  {
  case 400: // If read header returned 400, send 400
    send400(sockFd);
    return 0;
  case 404: // If read header returned 404, call send404
    send404(sockFd);
    return 0;
  case 200: // If the header was valid and the method was GET/HEAD, call send200()
    send200(sockFd, filename, requestData.type == HttpType::GET);
    return 0;
  case 201: // If the header was valid and the method was POST, call a function to save the file
    send201(sockFd);
    std::ofstream outFile(std::string("./data/" + filename));
    if (!outFile.is_open()) {
      ERROR << "Unable to write file: " << strerror(errno) << ENDL;
    }
    outFile.close();
    return 0;
  }

  return 1;
}
    

int main (int argc, char *argv[]) {
  // ********************************************************************
  // * Process the command line arguments
  // ********************************************************************
  int opt = 0;
  while ((opt = getopt(argc,argv,"d:")) != -1) {
    switch (opt) {
    case 'd':
      LOG_LEVEL = std::stoi(optarg);
      break;
    case ':':
    case '?':
    default:
      std::cout << "usage: " << argv[0] << " -d LOG_LEVEL" << std::endl;
      exit(-1);
    }
  }


  // *******************************************************************
  // * Catch all possible signals
  // ********************************************************************
  DEBUG << "Setting up signal handlers" << ENDL;
  signal(SIGINT, sig_handler); // Only interrupt is required in project.
  
  
  // *******************************************************************
  // * Creating the inital socket using the socket() call.
  // ********************************************************************
  int listenFd;
  if ((listenFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    ERROR << "Could not create socket: " << strerror(errno) << ENDL;
    exit(-1);
  }
  DEBUG << "Calling Socket() assigned file descriptor " << listenFd << ENDL;

  
  // ********************************************************************
  // * The bind() call takes a structure used to spefiy the details of the connection. 
  // *
  // * struct sockaddr_in servaddr;
  // *
  // On a client it contains the address of the server to connect to. 
  // On the server it specifies which IP address and port to lisen for connections.
  // If you want to listen for connections on any IP address you use the
  // address INADDR_ANY
  // ********************************************************************
  sockaddr_in servAddr;
  bzero(&servAddr, sizeof(servAddr));
  servAddr.sin_family = AF_INET; // IPv4
  servAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Let system pick IP addr


  // ********************************************************************
  // * Binding configures the socket with the parameters we have
  // * specified in the servaddr structure.  This step is implicit in
  // * the connect() call, but must be explicitly listed for servers.
  // *
  // * Don't forget to check to see if bind() fails because the port
  // * you picked is in use, and if the port is in use, pick a different one.
  // ********************************************************************
  uint16_t port = 1025;
  servAddr.sin_port = htons(port);
  DEBUG << "Calling bind()" << ENDL;
  while (bind(listenFd, (sockaddr*) &servAddr, sizeof(servAddr)) < 0) {
    if (errno != EADDRINUSE) {
      ERROR << "Unable to bind socket: " << strerror(errno) << ENDL;
      close(listenFd);
      exit(-1);
    }
    port++;
    servAddr.sin_port = htons(port);
  }
  std::cout << "Using port: " << port << std::endl;


  // ********************************************************************
  // * Setting the socket to the listening state is the second step
  // * needed to being accepting connections.  This creates a que for
  // * connections and starts the kernel listening for connections.
  // ********************************************************************
  DEBUG << "Calling listen()" << ENDL;
  int listenQueueSize = 1; // Assuming only 1 is needed for project scope.
  if (listen(listenFd, listenQueueSize) < 0) {
    ERROR << "Unable to listen with socket: " << strerror(errno) << ENDL;
    close(listenFd);
    exit(-1);
  }

  // ********************************************************************
  // * The accept call will sleep, waiting for a connection.  When 
  // * a connection request comes in the accept() call creates a NEW
  // * socket with a new fd that will be used for the communication.
  // ********************************************************************
  int quitProgram = 0;
  while (!quitProgram) {
    int connFd = 0;
    DEBUG << "Calling connFd = accept(fd,NULL,NULL)." << ENDL;

    if ((connFd = accept(listenFd, NULL, NULL)) < 0) {
      ERROR << "Failed to accept connection: " << strerror(errno) << ENDL;
      closefrom(3);
      exit(-1);
    }

    DEBUG << "We have recieved a connection on " << connFd << ". Calling processConnection(" << connFd << ")" << ENDL;
    quitProgram = processConnection(connFd);
    DEBUG << "processConnection returned " << quitProgram << " (should always be 0)" << ENDL;
    DEBUG << "Closing file descriptor " << connFd << ENDL;
    close(connFd);
  }

  ERROR << "Program fell through to the end of main. A listening socket may have closed unexpectedly." << ENDL;
  closefrom(3);
}
