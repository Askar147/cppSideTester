#include <iostream>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h> // For read/write/close

#include <chrono>
using namespace std::chrono;

#define POT_HIGH 1.0e10 

bool isLittleEndian() {
    uint16_t x = 1; // 0x0001
    auto ptr = reinterpret_cast<char*>(&x);
    return ptr[0] == 1; // Little endian if true
}

// Custom htonll implementation
uint64_t htonll(uint64_t value) {
    // Check if we're on a little-endian system; no change needed for big-endian
    if (isLittleEndian()) {
        // Swap bytes
        const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
        const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));
        
        // Combine parts into final value
        return (static_cast<uint64_t>(low_part) << 32) | high_part;
    } else {
        return value;
    }
}

// Helper function to convert double to network byte order
std::vector<unsigned char> doubleToBytes(double value) {
    std::vector<unsigned char> bytes(sizeof(double));
    uint64_t netValue = htonll(*reinterpret_cast<uint64_t*>(&value));
    std::memcpy(bytes.data(), &netValue, sizeof(double));
    return bytes;
}

ssize_t readFully(int sock, void* buf, size_t len) {
    size_t total = 0; // how many bytes we've read
    size_t bytesleft = len; // how many we have left to read
    ssize_t n;

    while(total < len) {
        n = read(sock, (char*)buf + total, bytesleft);
        if (n == -1) { break; } // Error
        if (n == 0) { break; } // Connection closed
        total += n;
        bytesleft -= n;
    }

    return n == -1 ? -1 : total; // return -1 on failure, total on success
}

struct Tuple {
    float floatValue;
    int intValue;
};

// Function to send data to Java server
void sendDataToJava(const char* serverIp, int port, const unsigned char* costs, size_t costsSize, 
                    double start_x, double start_y, double end_x, double end_y, int cycles) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return;
    }
    auto start = high_resolution_clock::now();

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, serverIp, &serverAddr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        return;
    }

    // Serialize data
    std::vector<unsigned char> buffer;
    auto appendDouble = [&](double value) {
        auto bytes = doubleToBytes(value);
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    };

    appendDouble(start_x);
    appendDouble(start_y);
    appendDouble(end_x);
    appendDouble(end_y);

    int netCycles = htonl(cycles);
    int netSize = htonl(costsSize);
    buffer.insert(buffer.end(), reinterpret_cast<unsigned char*>(&netCycles), reinterpret_cast<unsigned char*>(&netCycles) + sizeof(netCycles));
    buffer.insert(buffer.end(), reinterpret_cast<unsigned char*>(&netSize), reinterpret_cast<unsigned char*>(&netSize) + sizeof(netSize));
    buffer.insert(buffer.end(), costs, costs + costsSize);

    // Send data
    send(sock, buffer.data(), buffer.size(), 0);

    auto stop1 = high_resolution_clock::now();
    auto duration1 = duration_cast<milliseconds>(stop1 - start);
    std::cout << "Execution after sending data " << duration1.count() << std::endl;

    // Read the response from the Java server
    bool foundLegal;
    readFully(sock, &foundLegal, sizeof(foundLegal));
    
    
    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(stop - start);
    std::cout << "Execution after receiving first Boolean " << duration.count() << std::endl;

    // int byteArraySize;
    // readFully(sock, &byteArraySize, sizeof(byteArraySize));
    // byteArraySize = ntohl(byteArraySize); // Convert from network to host byte order

    // std::vector<unsigned char> byteArray(byteArraySize);
    // readFully(sock, byteArray.data(), byteArraySize);

    // // Assuming the system requires conversion from big-endian (network order)
    // std::vector<float> potentialArray(byteArraySize / sizeof(float));
    // for (size_t i = 0; i < potentialArray.size(); ++i) {
    //     uint32_t temp;
    //     std::memcpy(&temp, &byteArray[i * sizeof(float)], sizeof(float));
    //     temp = ntohl(temp); // Convert each float from network byte order
    //     std::memcpy(&potentialArray[i], &temp, sizeof(float));
    // }

    // // Print the received potential array
    // std::cout << "Received potential array (" << potentialArray.size() << " elements):" << std::endl;
    // for (size_t i = 0; i < potentialArray.size(); ++i) {
    //     if (potentialArray[i] != 1e+10){
    //         std::cout << potentialArray[i] << " ";
    //     }
    // }
    std::cout << std::endl;


    int length;
    readFully(sock, &length, sizeof(length));
    length = ntohl(length); // Convert from network byte order to host byte order

    // Allocate space for the tuples and read them
    std::vector<Tuple> tuples(length);
    for (int i = 0; i < length; ++i) {
        // Read the float part of the tuple
        uint32_t tempFloat;
        readFully(sock, &tempFloat, sizeof(tempFloat));
        tempFloat = ntohl(tempFloat); // Convert to host byte order
        memcpy(&tuples[i].floatValue, &tempFloat, sizeof(tempFloat));

        // Read the int part of the tuple
        readFully(sock, &tuples[i].intValue, sizeof(tuples[i].intValue));
        tuples[i].intValue = ntohl(tuples[i].intValue); // Convert to host byte order
    }

    // Print the received tuples for verification
    for (const Tuple& tuple : tuples) {
        std::cout << "Received tuple: (" << tuple.floatValue << ", " << tuple.intValue << ")" << std::endl;
    }

    float* pot = new float[416*160];
    std::fill(pot, pot + 416*160, POT_HIGH);

    if (pot == nullptr) {
        std::cerr << "Failed to allocate memory or obtain data." << std::endl;
        return; // Or handle the error as appropriate
    }

    for (int i = 0; i < tuples.size(); i++) {
        pot[tuples[i].intValue] = tuples[i].floatValue;
    }

    // for (int i = 0; i < 416*160; i++) {
    //     if (pot[i] != 1e+10){ 
    //         std::cout << "pot[" << i << "] = " << pot[i] << std::endl;
    //     }
    // }

    // Close socket
    
    close(sock);

    return;
}

int main() {
    const char* serverIp = "127.0.0.1";
    int port = 12345;
    // unsigned char costs[] = {1, 2,3,4,5};
    unsigned char costs[416*160] = {1}; // Example costs array

    for (int i =0; i < 416*160; i++){
        costs[i] = i % 10;
    }

    double start_x = 1.0, start_y = 2.0, end_x = 25.0, end_y = 25.0;
    int cycles = 1000;

    auto start = high_resolution_clock::now();
    
    sendDataToJava(serverIp, port, costs, sizeof(costs), start_x, start_y, end_x, end_y, cycles);


    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(stop - start);
    std::cout << "Execution in ms " << duration.count() << std::endl;

    return 0;
}
