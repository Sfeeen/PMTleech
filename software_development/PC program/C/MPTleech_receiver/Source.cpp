// Teensy Serial Monitor in C++ with Fast UI using Dear ImGui + SDL2 + Asio
// Replaces libserialport with header-only Asio for portable and high-speed serial communication

#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <asio.hpp>
#include <iostream>
#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>
#include <devguid.h>  // <-- Required for GUID_DEVCLASS_PORTS
#include "json.hpp"
#include <fstream>
#include <shlobj.h>     // SHCreateDirectoryEx
#include <commdlg.h>    // GetSaveFileName, GetOpenFileName
#include <bitset>



#pragma comment(lib, "setupapi.lib")

using asio::serial_port;
using asio::io_context;

using json = nlohmann::json;

std::string chip_name = "MyChip";
ImTextureID chip_texture = reinterpret_cast<ImTextureID>(nullptr);
SDL_Texture* sdl_chip_texture = nullptr;

struct Packet {
    uint64_t counter;
    std::string op;
    uint32_t address;
    uint32_t value;
};

std::vector<Packet> packet_buffer;
std::mutex packet_mutex;
std::atomic<bool> running{ false };
bool serial_thread_running = false;
uint64_t total_bytes = 0;
uint64_t total_packets = 0;
uint64_t valid_packets = 0;
uint64_t read_count = 0;
uint64_t write_count = 0;

std::vector<std::string> available_ports;
std::vector<std::string> port_labels;
int selected_port_index = 0;
std::thread reader;
io_context context;
serial_port port(context);

constexpr size_t MEMORY_SIZE = 64 * 1024;
uint8_t memory_data[MEMORY_SIZE];
bool memory_written[MEMORY_SIZE] = { false };  // Tracks if a byte is known

int selected_pin_count = 28;  // Default pin count

std::string teensy_ack_buffer;
std::string teensy_status_message;
bool teensy_running = false;

std::atomic<bool> streaming_mode_enabled{ false };

bool show_save_bin_popup = false;
char unknown_byte_input[5] = "FF";



const char* pin_options[] = {
    "NC", "/CS", "/WE", "/OE",
    "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15", "A16", "A17", "A18",
    "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "VCC", "GND"
};
constexpr int TOTAL_PINS = 32;
int selected_pin_function[TOTAL_PINS] = { 0 };  // All default to "NC"
int selected_mode = 0; // 0 = interrupt, 1 = continuous
int selected_freq_khz = 70; // default frequency in kHz (1 MHz)



int chip_img_width = 0;
int chip_img_height = 0;

std::vector<std::string> teensy_log_lines;
std::mutex teensy_log_mutex;
bool scroll_to_bottom = false;

bool show_teensy_config_popup = false;
std::string teensy_config_response;

std::vector<int> active_address_bits;
std::vector<int> active_data_bits;
int d1_bit_pos = -1;

int we_bit_pos = -1;
int oe_bit_pos = -1;




// Helper function to map chip pin number to Teensy pin based on chip size
int chip_pinnumber_to_teensy_pin(int chip_pin, int pin_count) {
    // Adjust pin index for centered layouts
    if (pin_count == 30) chip_pin += 1;
    else if (pin_count == 28) chip_pin += 2;

    // Map: chip pin -> teensy pin
    static const std::unordered_map<int, int> chip_to_teensy = {
        {1, 27}, {2, 39}, {3, 21}, {4, 23}, {5, 40}, {6, 15}, {7, 14},
        {8, 18}, {9, 19}, {10, 25}, {11, 24}, {12, 0}, {13, 1},
        {14, 35}, {15, 34}, {16, -1}, {17, 8}, {18, 32}, {19, 9},
        {20, 6}, {21, 13}, {22, 11}, {23, 16}, {24, 12}, {25, 22},
        {26, 17}, {27, 41}, {28, 20}, {29, 10}, {30, 26}, {31, 38}, {32, -1}
    };


    auto it = chip_to_teensy.find(chip_pin);
    if (it != chip_to_teensy.end()) return it->second;
    return -1; // Invalid pin
}

int teensy_pin_to_bit_position(int teensy_pin) {
    static const std::unordered_map<int, int> pin_to_bit = {
        {0, 3},   // T_GPIO1_3
        {1, 2},   // T_GPIO1_2
        {6, 42},  // T_GPIO2_10
        {8, 48},  // T_GPIO2_16
        {9, 43},  // T_GPIO2_11
        {10, 32}, // T_GPIO2_0
        {11, 34}, // T_GPIO2_2
        {12, 33}, // T_GPIO2_1
        {24, 12}, // T_GPIO1_12
        {25, 13}, // T_GPIO1_13
        {26, 30}, // T_GPIO1_30
        {27, 31}, // T_GPIO1_31
        {32, 44}, // T_GPIO2_12
        {23, 25}, // T_GPIO1_25
        {22, 24}, // T_GPIO1_24
        {21, 27}, // T_GPIO1_27
        {20, 26}, // T_GPIO1_26
        {19, 16}, // T_GPIO1_16
        {18, 17}, // T_GPIO1_17
        {17, 22}, // T_GPIO1_22
        {16, 23}, // T_GPIO1_23
        {15, 18}, // T_GPIO1_19
        {14, 19}, // T_GPIO1_18
        {13, 35}, // T_GPIO2_3
        {41, 21}, // T_GPIO1_21
        {40, 20}, // T_GPIO1_20
        {39, 29}, // T_GPIO1_29
        {38, 28}, // T_GPIO1_28
        {35, 60}, // T_GPIO2_28
        {34, 61}  // T_GPIO2_29
    };

    auto it = pin_to_bit.find(teensy_pin);
    if (it != pin_to_bit.end()) return it->second;
    return -1; // Invalid or unmapped pin
}

//void setup_active_gpio_bits() {
//    active_address_bits.clear();
//    active_data_bits.clear();
//    we_bit_pos = -1;
//    oe_bit_pos = -1;
//
//    int we_chip_pin = -1, oe_chip_pin = -1;
//    int we_teensy_pin = -1, oe_teensy_pin = -1;
//
//    std::vector<std::pair<std::string, int>> data_pin_labels_and_bits;
//
//    for (int i = 0; i < TOTAL_PINS; ++i) {
//        int idx = selected_pin_function[i];
//        if (idx < 0 || idx >= IM_ARRAYSIZE(pin_options)) continue;
//
//        std::string label = pin_options[idx];
//
//        // Reverse-calculate chip pin for mapping
//        int chip_pin = i + 1;
//        if (selected_pin_count == 30) chip_pin -= 1;
//        else if (selected_pin_count == 28) chip_pin -= 2;
//
//        int teensy_pin = chip_pinnumber_to_teensy_pin(chip_pin, selected_pin_count);
//        int bit = teensy_pin_to_bit_position(teensy_pin);
//        if (bit == -1) continue;
//
//        if (label.rfind("A", 0) == 0) {
//            active_address_bits.push_back(bit);
//        }
//        else if (label.rfind("D", 0) == 0) {
//            data_pin_labels_and_bits.emplace_back(label, bit);
//
//            std::cout << "[GPIO Setup] " << label
//                << " -> chip pin " << chip_pin
//                << ", teensy pin " << teensy_pin
//                << ", bit position " << bit << std::endl;
//        }
//        else if (label == "/WE") {
//            we_bit_pos = bit;
//            we_chip_pin = chip_pin;
//            we_teensy_pin = teensy_pin;
//        }
//        else if (label == "/OE") {
//            oe_bit_pos = bit;
//            oe_chip_pin = chip_pin;
//            oe_teensy_pin = teensy_pin;
//        }
//    }
//
//    std::sort(active_address_bits.begin(), active_address_bits.end());
//
//    // Sort D pins by label (D1 < D2 < D3 ...)
//    std::sort(data_pin_labels_and_bits.begin(), data_pin_labels_and_bits.end(), [](const auto& a, const auto& b) {
//        int a_num = std::stoi(a.first.substr(1)); // Skip 'D'
//        int b_num = std::stoi(b.first.substr(1));
//        return a_num < b_num;
//        });
//
//    for (const auto& [label, bit] : data_pin_labels_and_bits) {
//        active_data_bits.push_back(bit);
//    }
//}

void setup_active_gpio_bits() {
    active_address_bits.clear();
    active_data_bits.clear();
    we_bit_pos = -1;
    oe_bit_pos = -1;

    int we_chip_pin = -1, oe_chip_pin = -1;
    int we_teensy_pin = -1, oe_teensy_pin = -1;

    std::vector<std::pair<std::string, int>> data_pin_labels_and_bits;
    std::vector<std::pair<std::string, int>> address_pin_labels_and_bits;

    for (int i = 0; i < TOTAL_PINS; ++i) {
        int idx = selected_pin_function[i];
        if (idx < 0 || idx >= IM_ARRAYSIZE(pin_options)) continue;

        std::string label = pin_options[idx];

        // Reverse-calculate chip pin for mapping
        int chip_pin = i + 1;
        if (selected_pin_count == 30) chip_pin -= 1;
        else if (selected_pin_count == 28) chip_pin -= 2;

        int teensy_pin = chip_pinnumber_to_teensy_pin(chip_pin, selected_pin_count);
        int bit = teensy_pin_to_bit_position(teensy_pin);
        if (bit == -1) continue;

        if (label.rfind("A", 0) == 0) {
            address_pin_labels_and_bits.emplace_back(label, bit);
            std::cout << "[GPIO Setup] " << label
                << " -> chip pin " << chip_pin
                << ", teensy pin " << teensy_pin
                << ", bit position " << bit << std::endl;
        }
        else if (label.rfind("D", 0) == 0) {
            data_pin_labels_and_bits.emplace_back(label, bit);
            std::cout << "[GPIO Setup] " << label
                << " -> chip pin " << chip_pin
                << ", teensy pin " << teensy_pin
                << ", bit position " << bit << std::endl;
        }
        else if (label == "/WE") {
            we_bit_pos = bit;
            we_chip_pin = chip_pin;
            we_teensy_pin = teensy_pin;
        }
        else if (label == "/OE") {
            oe_bit_pos = bit;
            oe_chip_pin = chip_pin;
            oe_teensy_pin = teensy_pin;
        }
    }

    // Sort A pins by label (A1 < A2 < A3 ...)
    std::sort(address_pin_labels_and_bits.begin(), address_pin_labels_and_bits.end(), [](const auto& a, const auto& b) {
        int a_num = std::stoi(a.first.substr(1)); // Skip 'A'
        int b_num = std::stoi(b.first.substr(1));
        return a_num < b_num;
        });

    for (const auto& [label, bit] : address_pin_labels_and_bits) {
        active_address_bits.push_back(bit);
    }

    // Sort D pins by label (D1 < D2 < D3 ...)
    std::sort(data_pin_labels_and_bits.begin(), data_pin_labels_and_bits.end(), [](const auto& a, const auto& b) {
        int a_num = std::stoi(a.first.substr(1)); // Skip 'D'
        int b_num = std::stoi(b.first.substr(1));
        return a_num < b_num;
        });

    for (const auto& [label, bit] : data_pin_labels_and_bits) {
        active_data_bits.push_back(bit);
    }
}




std::string get_chip_configuration_status() {
    std::unordered_map<std::string, int> counts;
    std::vector<int> address_indices;
    std::vector<int> data_indices;

    for (int i = 0; i < TOTAL_PINS; ++i) {
        int idx = selected_pin_function[i];
        if (idx < 0 || idx >= IM_ARRAYSIZE(pin_options)) continue;

        std::string label = pin_options[idx];
        if (label == "NC" || label == "VCC" || label == "GND") continue;

        if (++counts[label] > 1)
            return "Invalid: Pin function '" + label + "' used more than once.";

        if (label[0] == 'A') {
            int num = std::atoi(&label[1]);
            address_indices.push_back(num);
        }
        else if (label[0] == 'D') {
            int num = std::atoi(&label[1]);
            data_indices.push_back(num);
        }
    }

    std::sort(address_indices.begin(), address_indices.end());
    std::sort(data_indices.begin(), data_indices.end());

    for (size_t i = 0; i < address_indices.size(); ++i)
        if (address_indices[i] != static_cast<int>(i + 1))
            return "Invalid: Missing A" + std::to_string(i + 1);

    for (size_t i = 0; i < data_indices.size(); ++i)
        if (data_indices[i] != static_cast<int>(i + 1))
            return "Invalid: Missing D" + std::to_string(i + 1);

    int addr_bits = static_cast<int>(address_indices.size());
    int data_bits = static_cast<int>(data_indices.size());
    int mem_size = (1 << addr_bits) * (data_bits / 8.0);

    std::string status = "Valid: " + std::to_string(data_bits) + " x " + std::to_string(addr_bits);
    status += " bits (" + std::to_string(mem_size) + " bytes)";
    return status;
}




void log_teensy_message(const std::string& line, bool from_pc = false) {
    std::lock_guard<std::mutex> lock(teensy_log_mutex);
    std::string tag = from_pc ? "[PC] " : "[Teensy] ";
    teensy_log_lines.push_back(tag + line);
    scroll_to_bottom = true;
    if (teensy_log_lines.size() > 500)
        teensy_log_lines.erase(teensy_log_lines.begin());
}


void load_chip_image(SDL_Renderer* renderer) {
    SDL_Surface* surface = SDL_LoadBMP("28chip.bmp");  // For now BMP; use stb_image for JPG/PNG
    if (surface) {
        sdl_chip_texture = SDL_CreateTextureFromSurface(renderer, surface);
        chip_texture = (ImTextureID)sdl_chip_texture;
        chip_img_width = surface->w;
        chip_img_height = surface->h;
        SDL_FreeSurface(surface);
    }
}

void ensure_chipconfig_directory_exists() {
    CreateDirectoryA("chipconfigs", NULL);  // Will not fail if already exists
}


void save_config_to_json() {
    char filename[MAX_PATH] = {};
    std::string suggested_name = chip_name + ".json";
    strncpy_s(filename, suggested_name.c_str(), suggested_name.size());

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "JSON files\0*.json\0All files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = ".\\chipconfigs";
    ofn.lpstrTitle = "Save Chip Configuration";
    ofn.Flags = 0;  // We will handle overwrite prompt manually

    if (GetSaveFileNameA(&ofn)) {
        std::string filepath = ofn.lpstrFile;

        // Ensure extension is .json
        if (filepath.size() < 5 || filepath.substr(filepath.size() - 5) != ".json") {
            filepath += ".json";
        }

        // If file exists, prompt to confirm overwrite
        std::ifstream test(filepath);
        if (test.good()) {
            int response = MessageBoxA(NULL, "File already exists. Overwrite?", "Confirm Overwrite", MB_YESNO | MB_ICONQUESTION);
            if (response != IDYES) return;
        }

        // Save JSON file
        json j;
        j["chip_name"] = chip_name;
        j["pin_count"] = selected_pin_count;
        for (int i = 0; i < TOTAL_PINS; ++i) {
            j["pins"].push_back(selected_pin_function[i]);
        }

        std::ofstream file(filepath);
        if (file) file << j.dump(4);
    }
}




void load_config_from_json(const std::string& filepath = "") {

    std::string file_to_open;

    if (!filepath.empty()) {
        file_to_open = filepath;
    }
    else {
        char filename[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFilter = "JSON files\0*.json\0All files\0*.*\0";
        ofn.lpstrInitialDir = ".\\chipconfigs";  // <-- Use relative path with ./ to prevent nesting
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = "Load Chip Configuration";
        ofn.Flags = OFN_FILEMUSTEXIST;

        if (!GetOpenFileNameA(&ofn)) return; // User cancelled
        file_to_open = filename;
    }

    std::ifstream file(file_to_open);
    if (!file) return;

    json j;
    file >> j;

    if (j.contains("chip_name")) chip_name = j["chip_name"];
    if (j.contains("pin_count")) selected_pin_count = j["pin_count"];
    if (j.contains("pins")) {
        auto pins = j["pins"];
        for (size_t i = 0; i < pins.size() && i < TOTAL_PINS; ++i) {
            selected_pin_function[i] = pins[i];
        }
    }
}




std::vector<std::string> enumerate_serial_ports(std::vector<std::string>& labels_out) {
    std::vector<std::string> ports;
    labels_out.clear();

    HDEVINFO device_info = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (device_info == INVALID_HANDLE_VALUE) return ports;

    SP_DEVINFO_DATA device_info_data;
    device_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

    char buffer[1024];
    DWORD size = 0;

    for (int i = 0; SetupDiEnumDeviceInfo(device_info, i, &device_info_data); ++i) {
        if (SetupDiGetDeviceRegistryPropertyA(device_info, &device_info_data, SPDRP_FRIENDLYNAME, NULL, (PBYTE)buffer, sizeof(buffer), &size)) {
            std::string name(buffer);
            size_t start = name.find("(COM");
            size_t end = name.find(")", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string port = name.substr(start + 1, end - start - 1); // COMxx
                std::string label = port + " - " + name.substr(0, start - 1);
                ports.push_back(port);
                labels_out.push_back(label);
            }
        }
    }
    SetupDiDestroyDeviceInfoList(device_info);
    return ports;
}

uint32_t extract_bits(uint64_t gpio64, const std::vector<int>& bits) {
    uint32_t result = 0;
    for (size_t i = 0; i < bits.size(); ++i) {
        if ((gpio64 >> bits[i]) & 1) {
            result |= (1 << i);
        }
    }
    return result;
}


std::string detect_operation(uint64_t gpio64) { // 32 WE // 33 OE
    // Debug print: binary format with space between lower and upper 32 bits
    std::bitset<64> bits(gpio64);
    std::string bin = bits.to_string();
    //std::cout << "GPIO64: " << bin.substr(0, 32) << " " << bin.substr(32) << std::endl;

    if (we_bit_pos >= 0 && ((gpio64 >> we_bit_pos) & 1) == 0) return "WRITE";
    if (oe_bit_pos >= 0 && ((gpio64 >> oe_bit_pos) & 1) == 0) return "READ";
    return "";
}


void serial_thread_func() {
    // Use global active_address_bits and active_data_bits set by setup_active_gpio_bits()

    #ifdef _WIN32
    // Boost thread priority on Windows
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    #endif


    uint64_t counter = 0;
    std::vector<uint8_t> buffer(8192);

    asio::error_code ec;

    while (running) {
        size_t bytes_read = port.read_some(asio::buffer(buffer), ec);
        if (ec) break;

        total_bytes += bytes_read;

        if (streaming_mode_enabled) {
            // 🔁 PACKET MODE
            if (bytes_read < 8) continue;
            int packet_count = static_cast<int>(bytes_read / 8);
            for (int i = 0; i < packet_count; ++i) {
                uint64_t gpio64 = *reinterpret_cast<uint64_t*>(&buffer[i * 8]);
                std::string op = detect_operation(gpio64);
                total_packets++;

                if (op.empty()) continue;
                valid_packets++;
                if (op == "READ") read_count++;
                else if (op == "WRITE") write_count++;

                uint32_t address = extract_bits(gpio64, active_address_bits);
                uint32_t value = extract_bits(gpio64, active_data_bits);


                Packet pkt{ ++counter, op, address, value };
                {
                    std::lock_guard<std::mutex> lock(packet_mutex);
                    packet_buffer.push_back(pkt);
                    if (packet_buffer.size() > 10) packet_buffer.erase(packet_buffer.begin());
                }

                if (address < MEMORY_SIZE) {
                    memory_data[address] = static_cast<uint8_t>(value & 0xFF);
                    memory_written[address] = true;
                }
            }
        }
        else {
            // 🔤 TEXT MODE
            for (size_t i = 0; i < bytes_read; ++i) {
                char c = static_cast<char>(buffer[i]);
                if (c == '\n' || c == '\r') {
                    if (!teensy_ack_buffer.empty()) {
                        std::string line = teensy_ack_buffer;

                        if (line == "ACK RUNNING") {
                            teensy_running = true;
                            teensy_status_message = "Running";
                        }
                        else if (line == "ACK IDLE") {
                            teensy_running = false;
                            teensy_status_message = "Idle";
                        }
                        else if (line.rfind("WE=", 0) == 0) {
                            teensy_status_message = line;

                            std::string parsed;
                            std::istringstream ss(line);
                            std::string token;
                            while (ss >> token) {
                                parsed += token + "\n";
                            }
                            teensy_config_response = parsed;
                            show_teensy_config_popup = true;
                        }

                        log_teensy_message(line, false);
                        teensy_ack_buffer.clear();
                    }
                }
                else {
                    teensy_ack_buffer += c;
                }
            }
        }
    }


    serial_thread_running = false;
}


int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Teensy Serial Monitor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 600, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    load_chip_image(renderer);
    ensure_chipconfig_directory_exists();
    load_config_from_json("chipconfigs/A276308A.json");


    available_ports = enumerate_serial_ports(port_labels);
    for (int i = 0; i < port_labels.size(); ++i) {
        if (port_labels[i].find("USB Serial Device") != std::string::npos) {
            selected_port_index = i;
            break;
        }
    }

    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) quit = true;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(950, 600), ImGuiCond_Once);
        ImGui::Begin("Memory view");

        if (ImGui::Button("Save bin file")) {
            strcpy_s(unknown_byte_input, "FF");  // Default fill for unknown
            show_save_bin_popup = true;
            ImGui::OpenPopup("Save Memory to Bin");
        }

        if (ImGui::BeginPopupModal("Save Memory to Bin", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter hex value to replace unknown (??) bytes:");
            ImGui::InputText("Hex byte", unknown_byte_input, IM_ARRAYSIZE(unknown_byte_input), ImGuiInputTextFlags_CharsHexadecimal);

            if (ImGui::Button("Proceed")) {
                unsigned int fill_byte = 0xFF;
                sscanf_s(unknown_byte_input, "%x", &fill_byte);

                std::string full_filename = chip_name;
                full_filename += "_spied.bin";  // simpler concatenation

                char filename[MAX_PATH] = {};
                strncpy_s(filename, sizeof(filename), full_filename.c_str(), _TRUNCATE);

                OPENFILENAMEA ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = NULL;
                ofn.lpstrFilter = "Binary files\0*.bin\0All files\0*.*\0";
                ofn.lpstrFile = filename;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrInitialDir = ".";
                ofn.lpstrTitle = "Save Memory Dump";
                ofn.Flags = OFN_OVERWRITEPROMPT;
                ofn.lpstrDefExt = "bin";  // <- ensures .bin gets added if user omits it


                if (GetSaveFileNameA(&ofn)) {
                    std::string path = ofn.lpstrFile;
                    // Ensure the extension is .bin
                    if (path.size() < 4 || path.substr(path.size() - 4) != ".bin") {
                        path += ".bin";
                    }

                    std::ofstream file(path, std::ios::binary);


                    if (file) {
                        for (size_t i = 0; i < MEMORY_SIZE; ++i) {
                            uint8_t b = memory_written[i] ? memory_data[i] : static_cast<uint8_t>(fill_byte);
                            file.write(reinterpret_cast<const char*>(&b), 1);
                        }
                        file.close();
                    }
                }

                show_save_bin_popup = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                show_save_bin_popup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }


        ImGui::BeginChild("HexViewer", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushFont(ImGui::GetFont());

        ImVec2 charSize = ImGui::CalcTextSize("FF");  // Approx width of 2 hex chars

        // Header
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Offset(h)");
        ImGui::SameLine(70);
        for (int col = 0; col < 16; ++col) {
            char col_label[4];
            snprintf(col_label, sizeof(col_label), "%02X", col);
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%s", col_label);
            if (col != 15) ImGui::SameLine();
        }

        // ASCII header
        float ascii_start_x = 70 + 16 * (charSize.x + 6) + 25;
        ImGui::SetCursorPosX(ascii_start_x);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Decoded text");

        // Hex + ASCII rows
        for (size_t row = 0; row < MEMORY_SIZE; row += 16) {
            // Offset label
            ImGui::Text("%08X", static_cast<unsigned int>(row));
            ImGui::SameLine(70);

            // Hex bytes
            for (int col = 0; col < 16; ++col) {
                size_t addr = row + col;
                if (addr < MEMORY_SIZE) {
                    if (memory_written[addr])
                        ImGui::Text("%02X", memory_data[addr]);
                    else
                        ImGui::TextDisabled("??");
                }
                else {
                    ImGui::Text("  ");
                }
                if (col != 15) ImGui::SameLine();
            }

            // ASCII preview
            ImGui::SameLine(ascii_start_x);
            for (int col = 0; col < 16; ++col) {
                size_t addr = row + col;
                char c = ' ';
                if (addr < MEMORY_SIZE && memory_written[addr]) {
                    c = memory_data[addr];
                    if (c < 32 || c > 126) c = '.';
                }
                ImGui::Text("%c", c);
                if (col != 15) ImGui::SameLine();
            }
        }

        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::End();

        //int active_tab = 0;

        //if (ImGui::BeginMainMenuBar()) {
        //    if (ImGui::BeginMenu("View")) {
        //        if (ImGui::MenuItem("Memory View", NULL, active_tab == 0)) active_tab = 0;
        //        if (ImGui::MenuItem("Packet Monitor", NULL, active_tab == 1)) active_tab = 1;
        //        if (ImGui::MenuItem("Chip Configuration", NULL, active_tab == 2)) active_tab = 2;
        //        ImGui::EndMenu();
        //    }
        //    ImGui::EndMainMenuBar();
        //}



        ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_Once);
        ImGui::Begin("Packet Monitor");

        if (ImGui::BeginCombo("Serial Port", port_labels.empty() ? "" : port_labels[selected_port_index].c_str())) {
            for (int i = 0; i < port_labels.size(); i++) {
                bool is_selected = (selected_port_index == i);
                if (ImGui::Selectable(port_labels[i].c_str(), is_selected)) {
                    selected_port_index = i;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Refresh Ports")) {
            available_ports = enumerate_serial_ports(port_labels);
            if (!available_ports.empty()) {
                selected_port_index = std::min(selected_port_index, static_cast<int>(available_ports.size() - 1));
            }
            else {
                selected_port_index = 0;
            }
        }

        ImGui::SameLine();
        if (!serial_thread_running && (!available_ports.empty() ? ImGui::Button("Connect") : (ImGui::BeginDisabled(), ImGui::Button("Connect")) && (ImGui::EndDisabled(), false))) {
            try {
                port.close();
                port.open(available_ports[selected_port_index].c_str());
                port.set_option(serial_port::baud_rate(2000000));
                port.set_option(serial_port::flow_control(serial_port::flow_control::none));
                port.set_option(serial_port::character_size(8));
                port.set_option(serial_port::parity(serial_port::parity::none));
                port.set_option(serial_port::stop_bits(serial_port::stop_bits::one));
                running = true;
                serial_thread_running = true;
                reader = std::thread(serial_thread_func);
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to open port: " << e.what() << std::endl;
            }
        }
        if (serial_thread_running && ImGui::Button("Disconnect")) {
            running = false;

            asio::error_code ec;

            if (port.is_open()) {
                port.cancel(ec); // cancel pending read
                port.close(ec);  // closes port
            }

            if (reader.joinable()) {
                reader.join();  // thread will exit due to ec from read_some
            }

            context.restart();  // reset asio for reuse
        }

        static bool teensy_streaming = false;

        if (serial_thread_running) {
            if (!teensy_streaming) {
                if (ImGui::Button("Start Streaming")) {
                    setup_active_gpio_bits();
                    streaming_mode_enabled = true; // Enable binary parsing
                    asio::write(port, asio::buffer("START\n", 6));
                    log_teensy_message("START", true);
                    teensy_streaming = true;
                }
            }
            else {
                if (ImGui::Button("Stop Streaming")) {
                    streaming_mode_enabled = false; // Disable binary parsing
                    asio::write(port, asio::buffer("STOP\n", 5));
                    log_teensy_message("STOP", true);
                    teensy_streaming = false;
                }

                // Draw buffer backlog status dot
                {
                    size_t buffer_size = packet_buffer.size();
                    const size_t warning_threshold = 500;

                    ImVec4 dot_color = (buffer_size > warning_threshold)
                        ? ImVec4(1.0f, 0.2f, 0.2f, 1.0f)  // Red
                        : ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // Green
                    ImGui::SameLine();
                    ImGui::Text("Buffer Status: ");
                    ImGui::SameLine();
                    ImGui::ColorButton("##buffer_dot", dot_color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(12, 12));

                    ImGui::SameLine();
                    ImGui::Text("%s", buffer_size > warning_threshold ? "Backlog detected" : "Healthy");
                }

            }
            ImGui::NewLine();

            if (teensy_streaming) ImGui::BeginDisabled();

            //ImGui::SameLine();
            if (ImGui::Button("Send Config")) {
                int we_teensy = -1, oe_teensy = -1;

                for (int i = 0; i < TOTAL_PINS; ++i) {
                    int pin_idx = selected_pin_function[i];
                    if (pin_idx < 0 || pin_idx >= IM_ARRAYSIZE(pin_options)) continue;

                    std::string label = pin_options[pin_idx];

                    int chip_pin = i + 1;
                    if (selected_pin_count == 30) chip_pin -= 1;
                    else if (selected_pin_count == 28) chip_pin -= 2;

                    if (label == "/WE") we_teensy = chip_pinnumber_to_teensy_pin(chip_pin, selected_pin_count);
                    if (label == "/OE") oe_teensy = chip_pinnumber_to_teensy_pin(chip_pin, selected_pin_count);
                }

                if (we_teensy != -1 || oe_teensy != -1) {
                    int mode = selected_mode;
                    int freq = selected_freq_khz * 1000;

                    std::string config_cmd = "CFG WE=" + std::to_string(we_teensy) +
                        " OE=" + std::to_string(oe_teensy) +
                        " MODE=" + std::to_string(mode) +
                        " FREQ=" + std::to_string(freq) +
                        " NAME=" + chip_name + "\n";

                    asio::write(port, asio::buffer(config_cmd));
                    log_teensy_message(config_cmd, true);
                }
                else {
                    std::cerr << "Missing both /WE and /OE pin assignments!\n";
                }

            }


            ImGui::SameLine();
            if (ImGui::Button("Get Config")) {
                asio::write(port, asio::buffer("GETCFG\n", 7));
                log_teensy_message("GETCFG", true);
            }

            if (teensy_streaming) ImGui::EndDisabled();

            //ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Teensy Status: %s", teensy_status_message.c_str());


            if (ImGui::Button("Clear All Data")) {
                {
                    std::lock_guard<std::mutex> lock(packet_mutex);
                    packet_buffer.clear();
                }
                total_packets = 0;
                valid_packets = 0;
                read_count = 0;
                write_count = 0;
                total_bytes = 0;
                for (size_t i = 0; i < MEMORY_SIZE; ++i) {
                    memory_written[i] = false;
                    memory_data[i] = 0;
                }
            }

        }


        static auto last_stats_time = std::chrono::high_resolution_clock::now();
        static double mbps = 0.0;
        static double mean_us = 0.0;
        static uint64_t last_valid_packet_count = 0;

        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_stats_time).count();

        if (elapsed >= 1.0) {
            mbps = (total_bytes * 8.0) / (elapsed * 1e6); // Megabits per second
            total_bytes = 0;

            uint64_t delta_packets = valid_packets - last_valid_packet_count;
            last_valid_packet_count = valid_packets;

            mean_us = (delta_packets > 0) ? (elapsed * 1e6) / delta_packets : 0.0;
            last_stats_time = now;
        }
        ImGui::NewLine();
        ImGui::Text("Transfer Rate: %.2f Mbps", mbps);
        ImGui::Text("Avg Time Between Ops: %.2f us", mean_us);
        ImGui::Text("Packets: %llu | Reads: %llu | Writes: %llu", total_packets, read_count, write_count);

        ImGui::Separator();
        ImGui::BeginChild("ScrollRegion", ImVec2(0, 400), true);
        std::lock_guard<std::mutex> lock(packet_mutex);
        for (const auto& pkt : packet_buffer) {
            ImGui::Text("%llu | %s | 0x%X | 0x%X", pkt.counter, pkt.op.c_str(), pkt.address, pkt.value);
        }
        ImGui::EndChild();
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_Once);
        ImGui::Begin("Chip Configuration");

        ImGui::InputText("Chip Name", &chip_name[0], 64);
        ImGui::RadioButton("28 pins", &selected_pin_count, 28); ImGui::SameLine();
        ImGui::RadioButton("30 pins", &selected_pin_count, 30); ImGui::SameLine();
        ImGui::RadioButton("32 pins", &selected_pin_count, 32);
        ImGui::Separator();

        ImGui::Text("Capture Mode:");
        ImGui::RadioButton("Interrupt", &selected_mode, 0); ImGui::SameLine();
        ImGui::RadioButton("Continuous", &selected_mode, 1);

        ImGui::Text("External Clock Frequency (1khz - 16Mhz) :");
        ImGui::InputInt("kHz", &selected_freq_khz);
        if (selected_freq_khz < 1) selected_freq_khz = 1;

        if (ImGui::Button("Save Config")) save_config_to_json();
        ImGui::SameLine();
        if (ImGui::Button("Load Config")) load_config_from_json();

        ImGui::Separator();
        ImGui::BeginChild("ChipLayout", ImVec2(0, 600), false);
        ImGui::Columns(3, NULL, false);

        ImVec4 vccColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);   // Red
        ImVec4 gndColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);   // White


        ImVec4 addrColor = ImVec4(0.8f, 0.8f, 0.1f, 1.0f);
        ImVec4 dataColor = ImVec4(0.2f, 0.9f, 0.3f, 1.0f);
        ImVec4 ctrlColor = ImVec4(0.3f, 0.5f, 0.9f, 1.0f);
        ImVec4 ncColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

        auto getColorForLabel = [&](const std::string& label) {
            if (label == "NC") return ncColor;
            if (label == "VCC") return vccColor;
            if (label == "GND") return gndColor;
            if (label == "/CS" || label == "/WE" || label == "/OE") return ctrlColor;
            if (label.rfind("A", 0) == 0) return addrColor;
            if (label.rfind("D", 0) == 0) return dataColor;
            return ImVec4(1, 1, 1, 1);  // default white
        };



        // Calculate vertical shift based on removed top pins
        float dropdown_height = ImGui::GetTextLineHeightWithSpacing();
        int pin_start_offset = (32 - selected_pin_count) / 2;
        ImGui::Dummy(ImVec2(0, pin_start_offset* (dropdown_height + 5)));

        // Render visible left-side pins (1 to N/2)
        for (int i = 0; i < selected_pin_count / 2; ++i) {
            int pin_index = i + pin_start_offset;

            ImGui::PushID(pin_index);

            // Last pin in left column becomes fixed GND
            if (i == (selected_pin_count / 2 - 1)) {
                // Force this pin to be GND (index 2 in pin_options)
                selected_pin_function[pin_index] = 2;

                ImVec4 gndColor = getColorForLabel("GND");
                ImGui::PushStyleColor(ImGuiCol_Text, gndColor);
                ImGui::BeginDisabled();
                
                int dummy_index = 0;
                ImGui::Combo(("Pin " + std::to_string(i + 1)).c_str(), &dummy_index, "GND");


                ImGui::EndDisabled();
                ImGui::PopStyleColor();

                selected_pin_function[pin_index] = -1;  // Optional: Mark internally as GND (not from dropdown options)
            }
            else {
                ImVec4 color = getColorForLabel(pin_options[selected_pin_function[pin_index]]);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Combo(("Pin " + std::to_string(i + 1)).c_str(), &selected_pin_function[pin_index], pin_options, IM_ARRAYSIZE(pin_options));
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }



        ImGui::NextColumn();

        // Middle image
        //if (chip_texture) {


        if (chip_texture) {
            float display_height = 350.0f;  // desired display height
            float aspect_ratio = (float)chip_img_width / chip_img_height;
            float display_width = display_height * aspect_ratio;

            //ImGui::Dummy(ImVec2(50, 20));
            ImGui::Dummy(ImVec2(0, pin_start_offset * (dropdown_height + 5)));
            ImGui::Image(chip_texture, ImVec2(display_width, display_height));
        }
        else {
            ImGui::Text("Chip image not loaded.");
        }


        ImGui::NextColumn();

        // Right pins: Top to bottom, descending pin numbers (e.g., 32 → 17)
        ImGui::Dummy(ImVec2(0, pin_start_offset * (dropdown_height + 5)));  // shift down

        for (int i = 0; i < selected_pin_count / 2; ++i) {
            int logical_pin_number = selected_pin_count - i;
            int pin_index = TOTAL_PINS - 1 - i - pin_start_offset;


            ImGui::PushID(pin_index);

            // Top-most right pin becomes fixed VCC
            if (i == 0) {
                ImVec4 vccColor = getColorForLabel("VCC");
                ImGui::PushStyleColor(ImGuiCol_Text, vccColor);
                ImGui::BeginDisabled();

                int dummy_index = 0;
                ImGui::Combo(("Pin " + std::to_string(logical_pin_number)).c_str(), &dummy_index, "VCC");

                ImGui::EndDisabled();
                ImGui::PopStyleColor();

                //selected_pin_function[pin_index] = -2;  // Optional: mark as fixed VCC internally
            }
            else {
                ImVec4 color = getColorForLabel(pin_options[selected_pin_function[pin_index]]);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Combo(("Pin " + std::to_string(logical_pin_number)).c_str(), &selected_pin_function[pin_index], pin_options, IM_ARRAYSIZE(pin_options));
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }





        ImGui::Columns(1);

        ImGui::Separator();
        std::string status = get_chip_configuration_status();
        ImGui::TextColored(
            status.rfind("Valid", 0) == 0 ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
            "%s", status.c_str()
        );


        ImGui::EndChild();
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(600, 250), ImGuiCond_FirstUseEver);
        ImGui::Begin("Teensy Log");

        if (ImGui::Button("Clear Log")) {
            std::lock_guard<std::mutex> lock(teensy_log_mutex);
            teensy_log_lines.clear();
        }
        ImGui::Separator();

        ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(teensy_log_mutex);
            for (const auto& line : teensy_log_lines) {
                ImGui::TextUnformatted(line.c_str());
            }

            if (scroll_to_bottom) {
                ImGui::SetScrollHereY(1.0f);
                scroll_to_bottom = false;
            }
        }
        ImGui::EndChild();
        ImGui::End();

        if (show_teensy_config_popup) {
            ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Always);  // Set width to 500 pixels
            ImGui::OpenPopup("Teensy Configuration");
            show_teensy_config_popup = false;
        }

        if (ImGui::BeginPopupModal("Teensy Configuration", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", teensy_config_response.c_str());
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }



        ImGui::Render();
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }

    running = false;

    // Close port first so read_some() unblocks
    if (port.is_open()) {
        asio::error_code ec;
        port.cancel(ec);  // stops ongoing reads
        port.close(ec);   // close safely
    }

    // Wait for thread to exit cleanly
    if (reader.joinable()) {
        reader.join();
    }

    context.restart();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
