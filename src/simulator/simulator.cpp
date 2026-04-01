#include "../dram/dram.hpp"
#include "../cache/cache.hpp"

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>

void loadFile(const std::string& filename, DRAM* dram) {
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cout << "Could not open file: " << filename << "\n";
        return;
    }

    std::string line;
    int address = 0;

    // Assumes file is just a sequence of ints, one per word
    // You may need to change this depending on your professor's file format
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::vector<int> words;
        int value;

        while (ss >> std::hex >> value) {
            words.push_back(value);
        }

        if (!words.empty()) {
            while (true) {
                auto result = dram->store(address, 0, words);
                if (!result.wait) break;
            }
            address += words.size();
        }
    }

    std::cout << "Loaded " << filename << "\n";
}

void loadDRAMFromFile(const std::string& filename, DRAM* dram) {
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }

    std::string lineText;
    int currentAddress = 0;

    while (std::getline(file, lineText)) {
        if (lineText.empty()) {
            continue;
        }

        std::stringstream ss(lineText);
        DRAM::Line lineData;
        std::string token;

        while (ss >> token) {
            if (!token.empty() && token.back() == ':') {
                continue;
            }

            int value = 0;
            if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
                value = static_cast<int>(std::stoul(token, nullptr, 16));
            } else {
                value = std::stoi(token);
            }

            lineData.push_back(value);
        }

        if (lineData.empty()) {
            continue;
        }

        if (static_cast<int>(lineData.size()) != dram->getLineSize()) {
            std::cerr << "Invalid line in file. Expected "
                      << dram->getLineSize()
                      << " words, but got "
                      << lineData.size()
                      << ".\n";
            return;
        }

        dram->setLineDirect(currentAddress, lineData);
        currentAddress += dram->getLineSize();
    }

    std::cout << "Loaded DRAM from file: " << filename << "\n";
}

void handleInput(const std::string& instruction, DRAM* dram, Cache* cache) {
    std::stringstream ss(instruction);

    std::vector<std::string> operands;
    std::string operand;

    while (ss >> operand) {
        operands.push_back(operand);
    }

    if (operands.empty()) {
        return;
    }

    // L filename
    if (operands[0] == "L") {
        if (operands.size() != 2) {
            std::cout << "Usage: L <filename>\n";
            return;
        }

        loadDRAMFromFile(operands[1], dram);
        return;
    }

    // V M start end
    // V C start end
    if (operands[0] == "V") {
        if (operands.size() != 4) {
            std::cout << "Usage: V M <startLine> <endLine>\n";
            std::cout << "   or: V C <startLine> <endLine>\n";
            return;
        }

        int startLine = std::stoi(operands[2]);
        int endLine = std::stoi(operands[3]);

        if (operands[1] == "C") {
            cache->dump(startLine, endLine);
        } else if (operands[1] == "M") {
            dram->dump(startLine, endLine);
        } else {
            std::cout << "Invalid view target. Use M or C.\n";
        }

        return;
    }

    // R M address
    // Reads through cache/memory hierarchy
    if (operands[0] == "R") {
        if (operands.size() != 3) {
            std::cout << "Usage: R M <address>\n";
            return;
        }

        int address = std::stoi(operands[2]);

        if (operands[1] == "M") {
            auto result = cache->load(address, Cache::StageId::MEM);

            if (result.wait) {
                std::cout << "Wait\n";
            } else {
                std::cout << "Done\n";
                std::cout << "Value: " << result.value << "\n";
            }
        } else {
            std::cout << "Invalid read target. Use M.\n";
        }

        return;
    }

    // W M address value
    if (operands[0] == "W") {
        if (operands.size() != 4) {
            std::cout << "Usage: W M <address> <value>\n";
            return;
        }

        int address = std::stoi(operands[2]);
        int value = std::stoi(operands[3]);

        if (operands[1] == "M") {
            auto result = cache->store(address, Cache::StageId::IF, value);

            if (result.wait) {
                std::cout << "Wait\n";
            } else {
                std::cout << "Done\n";
            }
        } else {
            std::cout << "Invalid write target. Use M.\n";
        }

        return;
    }

    std::cout << "Invalid instruction\n";
}

int main() {
    DRAM dram(16, 1, 3);

    Cache cache(
        8,
        1,
        1,
        &dram,
        Cache::WritePolicy::WRITE_THROUGH,
        Cache::AllocatePolicy::NO_WRITE_ALLOCATE
    );

    std::string userInput;
    while (true) {
        std::getline(std::cin, userInput);
        handleInput(userInput, &dram, &cache);
    }
};