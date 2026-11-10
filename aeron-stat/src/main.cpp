#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>

void scan_cnc_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[-] Error: Could not open shared memory file: " << filepath << "\n";
        return;
    }

    std::cout << "===================================================================\n";
    std::cout << " Scanning Shared Memory Channel Registrations                     \n";
    std::cout << "===================================================================\n\n";

    std::string current_string;
    char ch;

    while (file.get(ch)) {
        // Collect printable ASCII characters
        if (std::isprint(static_cast<unsigned char>(ch))) {
            current_string += ch;
        } else {
            // If we hit a non-printable character and have a valid channel string match
            if (current_string.length() > 10 && 
               (current_string.find("aeron:") != std::string::npos || current_string.find("127.0.0.1") != std::string::npos)) {
                std::cout << "  -> " << current_string << "\n";
            }
            current_string.clear();
        }
    }
}

int main() {
    scan_cnc_file("/dev/shm/aeron-vyevdokymenko/cnc.dat");
    return 0;
}
