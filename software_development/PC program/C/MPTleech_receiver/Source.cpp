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
#include <algorithm>
#include <filesystem>  // C++17

namespace fs = std::filesystem;

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
    bool foreign_chip = false;
};

std::vector<Packet> packet_buffer;
std::vector<Packet> full_transaction_log;

std::atomic<uint64_t> current_time_counter = 0;

std::string current_project_path;
std::string current_config_filename;



std::mutex packet_mutex;
std::atomic<bool> running{ false };
bool serial_thread_running = false;
std::atomic<bool> step_mode_enabled{ false };

uint64_t total_bytes = 0;
uint64_t total_packets = 0;
uint64_t valid_packets = 0;
uint64_t invalid_packets = 0;
uint64_t read_count = 0;
uint64_t write_count = 0;
uint64_t ignored_chip_packets = 0;


std::vector<std::string> available_ports;
std::vector<std::string> port_labels;
int selected_port_index = 0;
std::thread reader;
io_context context;
serial_port port(context);

constexpr size_t MEMORY_SIZE = 64 * 1024;
size_t chip_memory_size = MEMORY_SIZE; // Default fallback
uint8_t memory_data[MEMORY_SIZE];
bool memory_written[MEMORY_SIZE] = { false };  // Tracks if a byte is known
bool memory_written_by_write_op[MEMORY_SIZE] = { false };
// Add this near global memory definitions
enum class MemColor { NONE, READ, WRITE, FOREIGN };
MemColor memory_color[MEMORY_SIZE] = { MemColor::NONE };

struct MemorySnapshot {
    uint64_t counter;
    uint8_t memory[MEMORY_SIZE];
    bool written[MEMORY_SIZE];
    bool written_by_write[MEMORY_SIZE];
    MemColor color[MEMORY_SIZE];
};
std::vector<MemorySnapshot> memory_snapshots;
constexpr size_t SNAPSHOT_INTERVAL = 30000;

uint8_t read_memory_data[MEMORY_SIZE];

int selected_pin_count = 28;  // Default pin count

std::string teensy_ack_buffer;
std::string teensy_status_message;
bool teensy_running = false;

//std::atomic<bool> streaming_mode_enabled{ false };

bool breakpoint_enabled = false;
int breakpoint_op = 0;  // 0 = both, 1 = read, 2 = write
int breakpoint_chip = 0; // 0 = both, 1 = this chip, 2 = foreign
char breakpoint_address_hex[9] = "";  // max 0xFFFFFFFF
char breakpoint_value_hex[9] = "";

std::atomic<bool> breakpoint_triggered{ false };
std::atomic<uint64_t> breakpoint_trigger_time = 0;
std::atomic<bool> pending_breakpoint_stop{ false };
std::chrono::steady_clock::time_point breakpoint_trigger_timestamp;
std::atomic<bool> breakpoint_pending_rebuild = false;


static std::atomic<bool> teensy_streaming = false;
static int step_count = 1;



bool show_save_bin_popup = false;
char unknown_byte_input[5] = "FF";

std::string memory_transactions_filename = "transactions.mt";
std::ofstream mt_log_file;

bool user_overridden_slider = false;
std::vector<uint32_t> recent_addresses;

bool scroll_to_recent_address = false;

std::string project_name;
bool show_project_dialog = true;
bool project_selected = false;

std::string project_user_comments;


const char* pin_options[] = {
    "NC", "/CS", "/WE", "/OE",
    "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15", "A16", "A17", "A18",
    "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "VCC", "GND"
};
constexpr int TOTAL_PINS = 32;
int selected_pin_function[TOTAL_PINS] = { 0 };  // All default to "NC"
int selected_mode = 0; // 0 = interrupt, 1 = continuous
int cpu_idle_behavior = 0; // 0 = halt, 1 = run at XTAL
int selected_freq_khz = 70; // default frequency in kHz (1 MHz)
int xtal_freq_mhz = 16;



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
int cs_bit_pos = -1;



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
            //std::cout << "[GPIO Setup] " << label
            //    << " -> chip pin " << chip_pin
            //    << ", teensy pin " << teensy_pin
            //    << ", bit position " << bit << std::endl;
        }
        else if (label.rfind("D", 0) == 0) {
            data_pin_labels_and_bits.emplace_back(label, bit);
            //std::cout << "[GPIO Setup] " << label
            //    << " -> chip pin " << chip_pin
            //    << ", teensy pin " << teensy_pin
            //    << ", bit position " << bit << std::endl;
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
        else if (label == "/CS") {
            cs_bit_pos = bit;
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
    chip_memory_size = mem_size; // Set global

    std::string status = "Valid: " + std::to_string(data_bits) + " x " + std::to_string(addr_bits);
    char size_str[64];
    snprintf(size_str, sizeof(size_str), " bits (%u / 0x%X bytes)", mem_size, mem_size);
    status += size_str;
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

void teensy_write_raw(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << '<';
    oss << std::setw(4) << std::setfill('0') << std::hex << data.size();

    std::string header = oss.str();

    // Combine header + data into one buffer
    std::vector<uint8_t> full_packet(header.begin(), header.end());
    full_packet.insert(full_packet.end(), data.begin(), data.end());

    // Send everything in one write
    asio::write(port, asio::buffer(full_packet));

    log_teensy_message("< [sending RAW data: " + std::to_string(data.size()) + " bytes]", true);
}


void teensy_write(const std::string& payload) {
    std::ostringstream oss;

    // Header
    oss << '<';
    oss << std::setw(4) << std::setfill('0') << std::hex << payload.size();

    // Payload
    oss << payload;

    std::string formatted = oss.str();

    asio::write(port, asio::buffer(formatted));
    log_teensy_message(payload, true);
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

        // Save to chip_config.json in current project as well
        if (!current_project_path.empty()) {
            std::string project_save_path = current_project_path + "/chip_config.json";
            std::ofstream proj_file(project_save_path);
            if (proj_file) proj_file << j.dump(4);
        }
    }
}

void print_last_win_error() {
    DWORD error = GetLastError();
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer, 0, NULL);

    std::cerr << "WinAPI error: " << messageBuffer << "\n";
    LocalFree(messageBuffer);
}

void save_current_project() {
    if (current_project_path.empty()) {
        std::cerr << "[Save] No current project path set.\n";
        return;
    }

    // 1. Save chip configuration
    json chip_json;
    chip_json["chip_name"] = chip_name;
    chip_json["pin_count"] = selected_pin_count;
    for (int i = 0; i < TOTAL_PINS; ++i)
        chip_json["pins"].push_back(selected_pin_function[i]);

    std::ofstream chip_file(current_project_path + "/chip_config.json");
    if (chip_file) {
        chip_file << chip_json.dump(4);
    }
    else {
        std::cerr << "[Save] Failed to write chip_config.json\n";
    }

    // 2. Save meta.json
    json meta;
    meta["slider_counter"] = current_time_counter.load();
    meta["xtal_freq_mhz"] = xtal_freq_mhz;
    meta["selected_mode"] = selected_mode;
    meta["cpu_idle_behavior"] = cpu_idle_behavior;
    meta["selected_freq_khz"] = selected_freq_khz;
    meta["config_filename"] = current_config_filename;
    meta["user_comments"] = project_user_comments;


    std::ofstream meta_file(current_project_path + "/meta.json");
    if (meta_file) {
        meta_file << meta.dump(4);
    }
    else {
        std::cerr << "[Save] Failed to write meta.json\n";
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
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;  // ✅ Add this

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

    save_current_project();
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

void rebuild_memory_state_up_to(uint64_t target_counter) {
    if (!running) return;
    std::cout << "locky" << std::endl;
    std::lock_guard<std::mutex> lock(packet_mutex);
    std::cout << "locky done" << std::endl;

    std::cout << "rebuilding" << std::endl;
    recent_addresses.clear();

    // Find nearest snapshot
    size_t start_index = 0;
    uint64_t start_counter = 0;
    bool snapshot_found = false;
    for (int i = static_cast<int>(memory_snapshots.size()) - 1; i >= 0; --i) {
        if (memory_snapshots[i].counter <= target_counter) {
            memcpy(memory_data, memory_snapshots[i].memory, chip_memory_size);
            memcpy(memory_written, memory_snapshots[i].written, chip_memory_size);
            memcpy(memory_written_by_write_op, memory_snapshots[i].written_by_write, chip_memory_size);
            memcpy(memory_color, memory_snapshots[i].color, sizeof(memory_color));
            start_counter = memory_snapshots[i].counter;
            snapshot_found = true;
            break;
        }
    }

    // ✅ If no snapshot was found (e.g., time 0), clear memory to initial state
    if (!snapshot_found) {
        std::fill(std::begin(memory_data), std::end(memory_data), 0);
        std::fill(std::begin(memory_written), std::end(memory_written), false);
        std::fill(std::begin(memory_written_by_write_op), std::end(memory_written_by_write_op), false);
        std::fill(std::begin(memory_color), std::end(memory_color), MemColor::NONE);
        start_counter = 0;
    }

    for (const Packet& pkt : full_transaction_log) {
        if (pkt.counter < start_counter) continue;
        if (pkt.counter > target_counter) break;
        if (pkt.address >= chip_memory_size) continue;

        memory_data[pkt.address] = static_cast<uint8_t>(pkt.value & 0xFF);
        memory_written[pkt.address] = true;

        if (pkt.foreign_chip) {
            memory_color[pkt.address] = MemColor::FOREIGN;
        }
        else if (pkt.op == "WRITE") {
            memory_written_by_write_op[pkt.address] = true;
            memory_color[pkt.address] = MemColor::WRITE;
        }
        else if (pkt.op == "READ") {
            memory_color[pkt.address] = MemColor::READ;
        }

    }
    recent_addresses.clear();
    int count = 0;

    //std::cout << "[rebuild] Last 10 accessed addresses:\n";

    for (auto it = full_transaction_log.rbegin(); it != full_transaction_log.rend(); ++it) {
        if (it->counter > target_counter) continue;

        recent_addresses.push_back(it->address);
        //std::cout << "  0x" << std::hex << it->address << std::dec << "\n";

        if (++count >= 10) break;
    }
    
    std::reverse(recent_addresses.begin(), recent_addresses.end()); // maintain chronological order
    scroll_to_recent_address = !recent_addresses.empty();
    std::cout << "rebuilding done" << std::endl;
}

void stop_streaming() {
    if (teensy_streaming.load()) {
        //streaming_mode_enabled = false; // Disable binary parsing
        teensy_streaming.store(false);

        teensy_write("STOP");
        // Let Teensy flush and main thread to process idle state
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Updated serial_thread_func with robust packet parsing
void serial_thread_func() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

    uint64_t counter = 0;
    asio::error_code ec;
    std::vector<uint8_t> temp_buffer(8192);
    std::vector<uint8_t> input_buffer;

    while (running) {
        size_t bytes_read = port.read_some(asio::buffer(temp_buffer), ec);
        if (ec) break;
        total_bytes += bytes_read;

        input_buffer.insert(input_buffer.end(), temp_buffer.begin(), temp_buffer.begin() + bytes_read);

        while (true) {
            if (input_buffer.size() < 3) break;

            if (input_buffer[0] != '>') {
                // Resync to next possible packet start
                input_buffer.erase(input_buffer.begin());
                continue;
            }

            uint8_t first = input_buffer[1];

            if (first == 0xFF) {
                // Expect '>\xFF' + 8 byte gpio packet = 10 bytes
                if (input_buffer.size() < 10) break;

                uint64_t gpio64 = *reinterpret_cast<uint64_t*>(&input_buffer[2]);

                input_buffer.erase(input_buffer.begin(), input_buffer.begin() + 10);

                std::string op = detect_operation(gpio64);
                total_packets++;
                if (op.empty()) {
                    invalid_packets++;
                    if (mt_log_file.is_open()) {
                        mt_log_file << "INVALID 0x" << std::hex << gpio64 << std::dec << "\n";
                    }
                    continue;
                }
                valid_packets++;
                if (op == "READ") read_count++;
                else if (op == "WRITE") write_count++;

                bool foreign_chip = false;
                if (cs_bit_pos >= 0 && ((gpio64 >> cs_bit_pos) & 1) != 0) {
                    ignored_chip_packets++;
                    foreign_chip = true;
                }

                uint32_t address = extract_bits(gpio64, active_address_bits);
                uint32_t value = extract_bits(gpio64, active_data_bits);
                Packet pkt{ ++counter, op, address, value, foreign_chip };

                if (breakpoint_enabled && !breakpoint_triggered) {
                    bool match = true;
                    int safe_op = std::clamp(breakpoint_op, 0, 2);
                    int safe_chip = std::clamp(breakpoint_chip, 0, 2);

                    if (safe_op == 1 && pkt.op != "READ") match = false;
                    else if (safe_op == 2 && pkt.op != "WRITE") match = false;

                    if (safe_chip == 1 && pkt.foreign_chip) match = false;
                    else if (safe_chip == 2 && !pkt.foreign_chip) match = false;

                    if (strlen(breakpoint_address_hex) > 0) {
                        uint32_t target_addr = strtoul(breakpoint_address_hex, nullptr, 16);
                        if (pkt.address != target_addr) match = false;
                    }
                    if (strlen(breakpoint_value_hex) > 0) {
                        uint32_t target_val = strtoul(breakpoint_value_hex, nullptr, 16);
                        if (pkt.value != target_val) match = false;
                    }

                    if (match) {
                        std::cout << "matched" << std::endl;
                        stop_streaming();
                        breakpoint_triggered = true;
                        breakpoint_trigger_time = pkt.counter;
                        breakpoint_trigger_timestamp = std::chrono::steady_clock::now();
                        breakpoint_pending_rebuild = true;
                        std::cout << "end matched" << std::endl;
                    }
                }

                if (mt_log_file.is_open()) {
                    mt_log_file << pkt.op << " 0x" << std::hex << pkt.address << " 0x" << pkt.value << std::dec;
                    if (pkt.foreign_chip) mt_log_file << " foreign";
                    mt_log_file << "\n";
                }

                {
                    std::lock_guard<std::mutex> lock(packet_mutex);
                    packet_buffer.push_back(pkt);
                    full_transaction_log.push_back(pkt);
                    if (packet_buffer.size() > 10) packet_buffer.erase(packet_buffer.begin());
                }

                if (pkt.counter == 1 || pkt.counter % SNAPSHOT_INTERVAL == 0) {
                    MemorySnapshot snap;
                    snap.counter = pkt.counter;
                    memcpy(snap.memory, memory_data, chip_memory_size);
                    memcpy(snap.written, memory_written, chip_memory_size);
                    memcpy(snap.written_by_write, memory_written_by_write_op, chip_memory_size);
                    memcpy(snap.color, memory_color, sizeof(memory_color));
                    memory_snapshots.push_back(snap);
                }

                current_time_counter.store(pkt.counter);

                if (address < chip_memory_size) {
                    memory_data[address] = static_cast<uint8_t>(value & 0xFF);
                    memory_written[address] = true;
                }

            }
            else {
                // ASCII packet
                uint16_t msg_size = (input_buffer[1] << 8) | input_buffer[2];

                if (msg_size > 800) {
                    std::cerr << "Invalid message size: " << msg_size << ", discarding\n";
                    input_buffer.erase(input_buffer.begin());
                    continue;
                }

                if (input_buffer.size() < 3 + msg_size) break;

                std::string msg(reinterpret_cast<char*>(&input_buffer[3]), msg_size);
                input_buffer.erase(input_buffer.begin(), input_buffer.begin() + 3 + msg_size);

                if (!msg.empty()) {
                    char type = msg[0];
                    std::string content = msg.substr(1);

                    switch (type) {
                    case 'A':
                        step_mode_enabled = false;
                        std::cout << "stop step mode" << std::endl;
                        rebuild_memory_state_up_to(current_time_counter.load());
                        break;
                    case 'C': log_teensy_message("Config acknowledged: " + content, false); break;
                    case 'E': log_teensy_message("ERROR: " + content, false); break;
                    case 'I':
                        teensy_running = false;
                        teensy_status_message = "Idle";
                        log_teensy_message(content, false);
                        break;
                    case 'X': log_teensy_message("Reset confirmed: " + content, false); break;
                    case 'U': log_teensy_message("Unknown command: " + content, false); break;
                    case 'G':
                        log_teensy_message(content, false);
                        teensy_status_message = content;
                        {
                            std::string parsed;
                            std::istringstream ss(content);
                            std::string token;
                            while (ss >> token) parsed += token + "\n";
                            teensy_config_response = parsed;
                            show_teensy_config_popup = true;
                        }
                        break;
                    case 'R': {
                        if (content.size() < 4) {
                            log_teensy_message("Received 'R' message with invalid length", true);
                            break;
                        }
                        uint32_t addr =
                            (uint8_t)content[0] << 24 |
                            (uint8_t)content[1] << 16 |
                            (uint8_t)content[2] << 8 |
                            (uint8_t)content[3];

                        char logbuf[64];
                        snprintf(logbuf, sizeof(logbuf), "Received memory chunk starting at 0x%08X, %zu bytes", addr, content.size() - 4);
                        log_teensy_message(logbuf, false);

                        for (size_t i = 4; i < content.size(); ++i) {
                            read_memory_data[addr++] = (uint8_t)content[i];
                        }
                        break;
                    }
                    default:
                        log_teensy_message("Unhandled message type: '" + std::string(1, type) + "'", false);
                        break;
                    }
                }
            }
        }
    }
    serial_thread_running = false;
}


void save_project(const std::string& project_name) {
    
    current_project_path = "projects/" + project_name;
    std::cout << "save project: " + current_project_path << std::endl;
    std::string base_path = "projects/" + project_name;
    CreateDirectoryA("projects", NULL);
    CreateDirectoryA(base_path.c_str(), NULL);

    // 1. Save chip configuration
    json chip_json;
    chip_json["chip_name"] = chip_name;
    chip_json["pin_count"] = selected_pin_count;
    for (int i = 0; i < TOTAL_PINS; ++i)
        chip_json["pins"].push_back(selected_pin_function[i]);
    std::ofstream chip_file(base_path + "/chip_config.json");
    chip_file << chip_json.dump(4);

    // 3. Save meta.json
    json meta;
    meta["slider_counter"] = current_time_counter.load();
    meta["xtal_freq_mhz"] = xtal_freq_mhz;
    meta["selected_mode"] = selected_mode;
    meta["cpu_idle_behavior"] = cpu_idle_behavior;
    meta["selected_freq_khz"] = selected_freq_khz;

    std::ofstream meta_file(base_path + "/meta.json");
    meta_file << meta.dump(4);
}

void load_project(const std::string& project_name) {
    current_project_path = "projects/" + project_name;
    std::cout << "Load project: " + current_project_path << std::endl;

    available_ports = enumerate_serial_ports(port_labels);
    for (int i = 0; i < port_labels.size(); ++i) {
        if (port_labels[i].find("PMTLeech") != std::string::npos) {
            selected_port_index = i;

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
                std::cerr << "Auto-connect failed: " << e.what() << std::endl;
            }

            break;  // Only connect to the first matching one
        }
    }

    // 1. Load chip configuration
    std::ifstream meta_file(current_project_path + "/meta.json");
    uint64_t slider_pos = 0;
    if (meta_file) {
        json j; meta_file >> j;

        if (j.contains("slider_counter")) slider_pos = j["slider_counter"];
        if (j.contains("xtal_freq_mhz")) xtal_freq_mhz = j["xtal_freq_mhz"];
        if (j.contains("selected_mode")) selected_mode = j["selected_mode"];
        if (j.contains("cpu_idle_behavior")) cpu_idle_behavior = j["cpu_idle_behavior"];
        if (j.contains("selected_freq_khz")) selected_freq_khz = j["selected_freq_khz"];
        if (j.contains("user_comments")) project_user_comments = j["user_comments"];
    }

    // Reset comments buffer
    extern char comment_buffer[4096];  // Declare external if needed
    strncpy_s(comment_buffer, project_user_comments.c_str(), sizeof(comment_buffer));
    extern bool initialized;          // Reset initialization flag
    initialized = true;


    std::ifstream chip_file(current_project_path + "/chip_config.json");
    if (chip_file) {
        json j; chip_file >> j;
        chip_name = j["chip_name"];
        selected_pin_count = j["pin_count"];
        auto pins = j["pins"];
        for (size_t i = 0; i < pins.size() && i < TOTAL_PINS; ++i)
            selected_pin_function[i] = pins[i];
    }

    // 2. Load transaction log
    std::ifstream mt_file(current_project_path + "/transactions.mt");
    full_transaction_log.clear();
    packet_buffer.clear();
    memory_snapshots.clear();
    invalid_packets = 0;
    if (mt_file) {
        std::string line;
        uint64_t counter = 0;

        while (std::getline(mt_file, line)) {
            if (line.empty() || line[0] == '#') continue; // Skip comments or empty lines

            // Special case: INVALID lines
            if (line.rfind("INVALID", 0) == 0) {
                invalid_packets++;  // Count it as valid for stats
                continue;         // Skip logging the packet
            }

            std::istringstream ss(line);
            std::string op, addr_str, val_str, foreign_flag;

            if (!(ss >> op >> addr_str >> val_str)) {
                std::cerr << "[LoadProject] Skipping invalid line (missing fields): " << line << "\n";
                continue;
            }

            uint32_t addr = 0, val = 0;
            try {
                addr = std::stoul(addr_str, nullptr, 16);
                val = std::stoul(val_str, nullptr, 16);
            }
            catch (const std::exception& e) {
                std::cerr << "[LoadProject] Skipping line with invalid hex values: " << line << " (" << e.what() << ")\n";
                continue;
            }

            bool foreign = false;
            if (ss >> foreign_flag) {
                foreign = (foreign_flag == "foreign");
            }

            full_transaction_log.push_back({ ++counter, op, addr, val, foreign });

            if (counter % SNAPSHOT_INTERVAL == 0 || counter == 1) {
                MemorySnapshot snap;
                snap.counter = counter;
                memcpy(snap.memory, memory_data, chip_memory_size);
                memcpy(snap.written, memory_written, chip_memory_size);
                memcpy(snap.written_by_write, memory_written_by_write_op, chip_memory_size);
                memcpy(snap.color, memory_color, sizeof(memory_color));
                memory_snapshots.push_back(snap);
            }
        }
    }



    // 2b. Recalculate counters
    total_packets = full_transaction_log.size() + invalid_packets;
    valid_packets = 0;
    read_count = 0;
    write_count = 0;
    ignored_chip_packets = 0;

    for (const auto& pkt : full_transaction_log) {
        if (pkt.op == "READ") read_count++;
        else if (pkt.op == "WRITE") write_count++;

        if (pkt.op == "READ" || pkt.op == "WRITE") valid_packets++;
        if (pkt.foreign_chip) ignored_chip_packets++;
    }

    // 4. Rebuild memory and update UI state
    current_time_counter.store(slider_pos);
    rebuild_memory_state_up_to(slider_pos);
    user_overridden_slider = true;
}

void render_memory_view() {
    ImGui::SetNextWindowSize(ImVec2(700, 700), ImGuiCond_Once);
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
                    for (size_t i = 0; i < chip_memory_size; ++i) {
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

    uint64_t min_counter = 0;
    uint64_t max_counter = 0;
    {
        std::lock_guard<std::mutex> lock(packet_mutex);
        if (!full_transaction_log.empty())
            max_counter = full_transaction_log.back().counter;
    }

    uint64_t temp_counter = current_time_counter.load();
    if (ImGui::SliderScalar("Time", ImGuiDataType_U64, &temp_counter, &min_counter, &max_counter)) {
        user_overridden_slider = true;
        current_time_counter.store(temp_counter);
        rebuild_memory_state_up_to(temp_counter);
    }

    ImGui::SameLine();
    if (ImGui::ArrowButton("##back", ImGuiDir_Left)) {
        uint64_t t = current_time_counter.load();
        if (t > 0) {
            t--;
            current_time_counter.store(t);
            rebuild_memory_state_up_to(t);
        }
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##forward", ImGuiDir_Right)) {
        uint64_t t = current_time_counter.load();
        if (t < max_counter) {
            t++;
            current_time_counter.store(t);
            rebuild_memory_state_up_to(t);
        }
    }

    // HEADER SECTION - always visible
    //ImGui::Indent(10.0f);  // Align first column
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Offset(h)");
    ImGui::SameLine(78);
    for (int col = 0; col < 16; ++col) {
        char col_label[4];
        snprintf(col_label, sizeof(col_label), "%02X", col);
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%s", col_label);
        if (col != 15) ImGui::SameLine();
    }

    ImVec2 charSize = ImGui::CalcTextSize("FF");  // Approx width of 2 hex chars

    // ASCII header (placed below hex header)
    float ascii_start_x = 70 + 16 * (charSize.x + 6) + 25;
    ImGui::SetCursorPosX(ascii_start_x);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Decoded text");
    //ImGui::Unindent(70.0f);  // Always unindent after

    // Add a small separator line or spacing to visually divide
    ImGui::Separator();

    ImGui::BeginChild("HexViewer", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (scroll_to_recent_address && !recent_addresses.empty()) {
        int latest_addr = recent_addresses.back();
        float line_height = ImGui::GetTextLineHeightWithSpacing();
        float scroll_y = (latest_addr / 16) * line_height;
        ImGui::SetScrollY(scroll_y - ImGui::GetWindowHeight() * 0.5f);  // Center on it
        scroll_to_recent_address = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushFont(ImGui::GetFont());

    // Constants
    float line_height = ImGui::GetTextLineHeightWithSpacing();
    int total_rows = static_cast<int>(chip_memory_size / 16);

    // Calculate visible rows
    int prebuffer_rows = 2;  // Draw 2 extra rows above
    int first_row = std::max(0, static_cast<int>(ImGui::GetScrollY() / line_height) - prebuffer_rows);
    int visible_rows = static_cast<int>(ImGui::GetWindowHeight() / line_height);
    int last_row = std::min(first_row + visible_rows + 3, total_rows); // +2 for margin

    // Add vertical spacing to simulate skipped lines
    ImGui::Dummy(ImVec2(0, first_row * line_height));

    for (int row = first_row; row < last_row; ++row) {
        size_t base_addr = row * 16;

        ImGui::Text("%08X", static_cast<unsigned int>(base_addr));
        ImGui::SameLine(70);

        for (int col = 0; col < 16; ++col) {
            size_t addr = base_addr + col;
            if (addr >= chip_memory_size) continue;

            bool is_recent = false;
            int recent_index = -1;
            for (int i = static_cast<int>(recent_addresses.size()) - 1; i >= 0; --i) {
                if (recent_addresses[i] == addr) {
                    is_recent = true;
                    recent_index = i;
                    break;
                }
            }

            // Adjust index if fewer than 10 recent addresses
            if (recent_index != -1 && recent_addresses.size() < 10) {
                recent_index += (10 - static_cast<int>(recent_addresses.size()));
            }

            if (memory_written[addr]) {

                if (is_recent) {
                    static const ImVec4 red_shades[10] = {
                        ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ImVec4(1.0f, 0.2f, 0.2f, 1.0f), ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                        ImVec4(1.0f, 0.4f, 0.4f, 1.0f), ImVec4(1.0f, 0.5f, 0.5f, 1.0f), ImVec4(1.0f, 0.6f, 0.6f, 1.0f),
                        ImVec4(1.0f, 0.7f, 0.7f, 1.0f), ImVec4(1.0f, 0.8f, 0.8f, 1.0f), ImVec4(1.0f, 0.9f, 0.9f, 1.0f),
                        ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                    };
                    ImVec4 shade = red_shades[9 - std::min(recent_index, 9)];
                    ImGui::TextColored(shade, "%02X", memory_data[addr]);
                }
                else {
                    ImVec4 color;
                    switch (memory_color[addr]) {
                    case MemColor::READ: color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
                    case MemColor::WRITE: color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
                    case MemColor::FOREIGN: color = ImVec4(0.8f, 0.4f, 1.0f, 1.0f); break;
                    default: color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
                    }
                    ImGui::TextColored(color, "%02X", memory_data[addr]);
                }
            }
            else {
                ImGui::TextDisabled("??");
            }

            if (col != 15) ImGui::SameLine();
        }

        // ASCII preview
        ImGui::SameLine(ascii_start_x + 10);
        for (int col = 0; col < 16; ++col) {
            size_t addr = base_addr + col;
            char c = ' ';
            if (addr < chip_memory_size && memory_written[addr]) {
                c = memory_data[addr];
                if (c < 32 || c > 126) c = '.';
            }
            ImGui::Text("%c", c);
            if (col != 15) ImGui::SameLine();
        }
    }

    // Dummy spacing for invisible lines below
    ImGui::Dummy(ImVec2(0, (total_rows - last_row) * line_height));


    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::End();
}

char comment_buffer[4096] = "";
bool initialized = false;

void render_comments_window() {
    ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_Once);
    ImGui::Begin("Comments");

    ImGui::Text("Add your notes or comments about the project:");

    if (!initialized) {
        strncpy_s(comment_buffer, project_user_comments.c_str(), sizeof(comment_buffer));
        initialized = true;
    }

    // Editable multi-line text area
    if (ImGui::InputTextMultiline("##comments", comment_buffer, sizeof(comment_buffer), ImVec2(-1, -1))) {
        project_user_comments = comment_buffer;
    }

    ImGui::End();
}



void send_config() {
    int we_teensy = -1, oe_teensy = -1, cs_teensy = -1;


    std::vector<std::pair<std::string, int>> address_pins;
    std::vector<std::pair<std::string, int>> data_pins;

    for (int i = 0; i < TOTAL_PINS; ++i) {
        int pin_idx = selected_pin_function[i];
        if (pin_idx < 0 || pin_idx >= IM_ARRAYSIZE(pin_options)) continue;

        std::string label = pin_options[pin_idx];

        int chip_pin = i + 1;
        if (selected_pin_count == 30) chip_pin -= 1;
        else if (selected_pin_count == 28) chip_pin -= 2;

        int teensy_pin = chip_pinnumber_to_teensy_pin(chip_pin, selected_pin_count);

        if (label == "/WE") we_teensy = teensy_pin;
        else if (label == "/OE") oe_teensy = teensy_pin;
        else if (label == "/CS") cs_teensy = teensy_pin;
        else if (label.rfind("A", 0) == 0) address_pins.emplace_back(label, teensy_pin);
        else if (label.rfind("D", 0) == 0) data_pins.emplace_back(label, teensy_pin);
    }

    if (we_teensy != -1 || oe_teensy != -1) {
        int mode = selected_mode;
        int freq = selected_freq_khz * 1000;
        int xtal_freq = xtal_freq_mhz * 1000000;

        // Sort A and D pins by label (A1 < A2 < A3, D1 < D2 < D3 ...)
        std::sort(address_pins.begin(), address_pins.end(), [](const auto& a, const auto& b) {
            return std::stoi(a.first.substr(1)) < std::stoi(b.first.substr(1));
            });

        std::sort(data_pins.begin(), data_pins.end(), [](const auto& a, const auto& b) {
            return std::stoi(a.first.substr(1)) < std::stoi(b.first.substr(1));
            });

        std::string addrs = "ADDRS=";
        for (size_t i = 0; i < address_pins.size(); ++i) {
            if (i > 0) addrs += ",";
            addrs += std::to_string(address_pins[i].second);
        }

        std::string datas = "DATAS=";
        for (size_t i = 0; i < data_pins.size(); ++i) {
            if (i > 0) datas += ",";
            datas += std::to_string(data_pins[i].second);
        }

        std::string config_cmd = "CFG WE=" + std::to_string(we_teensy) +
            " OE=" + std::to_string(oe_teensy) +
            " CS=" + std::to_string(cs_teensy) +
            " MODE=" + std::to_string(mode) +
            " FREQ=" + std::to_string(freq) +
            " XTAL=" + std::to_string(xtal_freq) +
            " IDLE=" + std::to_string(cpu_idle_behavior) +
            " " + addrs +
            " " + datas +
            " NAME=" + chip_name + "\n";


        teensy_write(config_cmd);
    }
    else {
        std::cerr << "Missing both /WE and /OE pin assignments!\n";
    }
}

void render_read_write_memory() {
    ImGui::SetNextWindowSize(ImVec2(700, 700), ImGuiCond_Once);
    ImGui::Begin("Read/Write Memory");

    // Optional: Add Write/Modify UI later here
    if (ImGui::Button("Read chip memory")) {
        std::fill(std::begin(read_memory_data), std::end(read_memory_data), 0xFF);
        teensy_write("READ_0x0000_0x00");
    }

    ImGui::SameLine();

    if (ImGui::Button("Write chip memory")) {
        try {
            const size_t MAX_CHUNK_SIZE = 500;

            size_t base_addr = 0;

            while (base_addr < chip_memory_size) {
                size_t chunk_len = std::min(MAX_CHUNK_SIZE, chip_memory_size - base_addr);

                // Construct the binary message
                std::vector<uint8_t> message;

                // Command header: WRITE + 3-byte address + 2-byte chunk length
                message.push_back('W');
                message.push_back('R');
                message.push_back('I');
                message.push_back('T');
                message.push_back('E');

                message.push_back((base_addr >> 16) & 0xFF);
                message.push_back((base_addr >> 8) & 0xFF);
                message.push_back(base_addr & 0xFF);

                message.push_back((chunk_len >> 8) & 0xFF);
                message.push_back(chunk_len & 0xFF);

                // Append the raw data
                message.insert(message.end(), read_memory_data + base_addr, read_memory_data + base_addr + chunk_len);

                // Send
                teensy_write_raw(message);

                // Delay 10 milliseconds
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                base_addr += chunk_len;

                //// For testing: only do 1 chunk
                //break;
            }

            log_teensy_message("Write complete from pc!", true);
            teensy_write("PSRAM_TO_CHIP_WRITE");
        }
        catch (const std::exception& e) {
            log_teensy_message(std::string("Write failed: ") + e.what(), false);
        }
    }


    if (ImGui::Button("Save Readout to Bin")) {
        char filename[MAX_PATH] = {};
        strcpy_s(filename, "chip_readout.bin");

        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFilter = "Binary files\0*.bin\0All files\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = ".";
        ofn.lpstrTitle = "Save Readout";
        ofn.Flags = OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = "bin";

        if (GetSaveFileNameA(&ofn)) {
            std::ofstream file(ofn.lpstrFile, std::ios::binary);
            if (file) {
                file.write(reinterpret_cast<const char*>(read_memory_data), chip_memory_size);
                file.close();
            }
        }
    }

    static int edit_byte_addr = -1;
    static char hex_input[5] = "";
    bool open_edit_popup = false;

    // Load File Button
    if (ImGui::Button("Load file into memory")) {
        char filename[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "Binary files\0*.bin\0All files\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = "Select a file to load";
        ofn.Flags = OFN_FILEMUSTEXIST;

        if (GetOpenFileNameA(&ofn)) {
            std::ifstream in(filename, std::ios::binary);
            if (in) {
                std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(in), {});
                size_t len = std::min(buffer.size(), static_cast<size_t>(chip_memory_size));
                std::copy(buffer.begin(), buffer.begin() + len, read_memory_data);
            }
        }
    }

    // --- Header (OUTSIDE the scrollable region) ---
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Offset(h)");
    ImGui::SameLine(78);
    for (int col = 0; col < 16; ++col) {
        char col_label[4];
        sprintf_s(col_label, "%02X", col);
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%s", col_label);
        if (col != 15) ImGui::SameLine();
    }

    ImVec2 cell_size = ImVec2(ImGui::CalcTextSize("FF").x + 2.0f, ImGui::GetTextLineHeight());
    float rw_ascii_start_x = 70 + 16 * (cell_size.x + 6.0f) + 25;

    ImGui::SetCursorPosX(rw_ascii_start_x);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Decoded text");

    ImGui::Separator();  // Visual separation


    ImGui::BeginChild("ReadMemoryHexViewer", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    float line_height = ImGui::GetTextLineHeightWithSpacing();
    int total_rows = static_cast<int>(chip_memory_size / 16);

    int prebuffer_rows = 2;
    int first_row = std::max(0, static_cast<int>(ImGui::GetScrollY() / line_height) - prebuffer_rows);
    int visible_rows = static_cast<int>(ImGui::GetWindowHeight() / line_height);
    int last_row = std::min(first_row + visible_rows + 3, total_rows);

    ImGui::Dummy(ImVec2(0, first_row * line_height));  // Skip lines above

    for (int row = first_row; row < last_row; ++row) {
        size_t base_addr = row * 16;

        ImGui::Text("%08X", static_cast<unsigned int>(base_addr));
        ImGui::SameLine(70);

        for (int col = 0; col < 16; ++col) {
            size_t addr = base_addr + col;
            char label[4];
            sprintf_s(label, "%02X", read_memory_data[addr]);
            ImGui::Text("%s", label);
            if (col != 15) ImGui::SameLine();
        }

        ImGui::SameLine(rw_ascii_start_x - 22);
        for (int col = 0; col < 16; ++col) {
            size_t addr = base_addr + col;
            char c = (read_memory_data[addr] >= 32 && read_memory_data[addr] <= 126) ? read_memory_data[addr] : '.';
            ImGui::Text("%c", c);
            if (col != 15) ImGui::SameLine();
        }
    }

    ImGui::Dummy(ImVec2(0, (total_rows - last_row) * line_height));  // Skip lines below


    ImGui::EndChild();
    ImGui::End();
}

void open_project_in_file_explorer() {
    try {
        std::string absolute_path = fs::absolute(current_project_path).string();
        std::string cmd = "explorer \"" + absolute_path + "\"";
        std::cout << cmd << "\n";
        std::system(cmd.c_str());
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to open file explorer: " << e.what() << "\n";
    }
}

void disconnect() {
    stop_streaming();         // ✅ Stop streaming and close MT file
    running = false;          // ✅ Tell thread to exit
    if (mt_log_file.is_open()) {
        mt_log_file.close();
        std::cout << "[Stop] Closed MT log file\n";
    }

    if (port.is_open()) {
        asio::error_code ec;
        port.cancel(ec);      // ✅ Unblock read_some()
    }

    if (reader.joinable()) {
        reader.join();        // ✅ Safe to wait for thread to finish
    }

    if (port.is_open()) {
        asio::error_code ec;
        port.close(ec);       // ✅ Now safe to close the port
    }

    context.restart();        // ✅ Reset ASIO for reuse
}



void close_project() {
    save_current_project();
    disconnect();
    project_selected = false;
    show_project_dialog = true;
    ImGui::OpenPopup("Select Project");
}

void render_gui() {
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("Save Project [Ctrl + s]")) {
                save_current_project();
            }

            if (ImGui::MenuItem("Close Project [Ctrl + x]")) {
                close_project();
            }

            if (ImGui::MenuItem("Open in File Explorer [Ctrl + o]")) {
                open_project_in_file_explorer();
            }

            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }


    if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
        save_current_project();
    }
    if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X))) {
        close_project();
    }

    if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))) {
        open_project_in_file_explorer();
    }

    render_memory_view();

    render_comments_window();


    ImGui::SetNextWindowSize(ImVec2(500, 700), ImGuiCond_Once);
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
        disconnect();
    }

    if (serial_thread_running) {
        ImGui::SameLine();
        if (teensy_streaming.load()) ImGui::BeginDisabled();

        //ImGui::SameLine();
        if (ImGui::Button("Send Config")) {
            send_config();
        }

        ImGui::SameLine();
        if (ImGui::Button("Get Config")) {
            teensy_write("GETCFG");
        }

        if (teensy_streaming.load()) ImGui::EndDisabled();

        if (!teensy_streaming.load()) {
            if (ImGui::Button("Start Streaming")) {
                send_config();
                setup_active_gpio_bits();
                //streaming_mode_enabled = true;

                // Reset file stream safely
                if (mt_log_file.is_open()) mt_log_file.close();
                mt_log_file.clear();
                std::string mt_path = current_project_path + "/" + memory_transactions_filename;
                mt_log_file.open(mt_path, std::ios::out | std::ios::app); 

                teensy_streaming.store(true);
                teensy_write("START");
            }

            ImGui::SameLine();

            ImGui::SetNextItemWidth(80);
            ImGui::InputInt("##step_count", &step_count);
            if (step_count < 1) step_count = 1;

            ImGui::SameLine();

            if (ImGui::Button("CLK Step")) {
                std::string cmd = "STEP_" + std::to_string(step_count) + "\n";
                teensy_write(cmd);
                step_mode_enabled = true;  // 👈 Enable step mode
            }
            ImGui::SameLine();
            if (ImGui::Button("INSTR Step")) {
                std::string cmd = "INSTR_" + std::to_string(step_count) + "\n";
                teensy_write(cmd);
                step_mode_enabled = true;  // 👈 Enable step mode
            }

            ImGui::Checkbox("Enable Streaming Breakpoint", &breakpoint_enabled);

            ImGui::Text("Stop if packet matches:");
            ImGui::SameLine();
            ImGui::Text("Operation:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::Combo("##op", &breakpoint_op, "Both\0READ\0WRITE\0");

            ImGui::SameLine();
            ImGui::Text("Chip:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::Combo("##chip", &breakpoint_chip, "Both\0This chip\0Foreign\0");

            ImGui::Text("Address (hex):");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("##addr", breakpoint_address_hex, IM_ARRAYSIZE(breakpoint_address_hex), ImGuiInputTextFlags_CharsHexadecimal);

            ImGui::SameLine();
            ImGui::Text("Value (hex):");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("##val", breakpoint_value_hex, IM_ARRAYSIZE(breakpoint_value_hex), ImGuiInputTextFlags_CharsHexadecimal);


            if (ImGui::Button("Reset CPU")) {
                std::string cmd = "RESET_CPU \n";
                teensy_write(cmd);
            }
        }

        else {
            if (ImGui::Button("Stop Streaming")) {
                stop_streaming();
                rebuild_memory_state_up_to(current_time_counter);
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

        if (ImGui::Button("Save MT file")) {
            char filename[MAX_PATH] = {};
            strcpy_s(filename, "transactions.mt");

            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = NULL;
            ofn.lpstrFilter = "Memory Transactions\0*.mt\0All Files\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = "Save Memory Transactions";
            ofn.lpstrDefExt = "mt";
            ofn.Flags = OFN_OVERWRITEPROMPT;

            if (GetSaveFileNameA(&ofn)) {
                std::ifstream temp_in(current_project_path + "/" + memory_transactions_filename, std::ios::binary);
                std::ofstream perm_out(ofn.lpstrFile, std::ios::binary);
                perm_out << temp_in.rdbuf();
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("Clear All Data")) {
            stop_streaming();  // prevent concurrent writes
            {
                std::lock_guard<std::mutex> lock(packet_mutex);
                packet_buffer.clear();
                full_transaction_log.clear();
            }

            // Clear memory contents and flags
            std::fill(std::begin(memory_data), std::end(memory_data), 0);
            std::fill(std::begin(memory_written), std::end(memory_written), false);
            std::fill(std::begin(memory_written_by_write_op), std::end(memory_written_by_write_op), false);
            std::fill(std::begin(memory_color), std::end(memory_color), MemColor::NONE);

            // Clear snapshots and recent addresses
            memory_snapshots.clear();
            recent_addresses.clear();
            scroll_to_recent_address = false;

            // Reset counters
            total_bytes = 0;
            total_packets = 0;
            valid_packets = 0;
            invalid_packets = 0;
            read_count = 0;
            write_count = 0;
            ignored_chip_packets = 0;

            // Reset time travel state
            current_time_counter.store(0);
            user_overridden_slider = false;

            if (mt_log_file.is_open()) {
                mt_log_file.close();
                std::cout << "[Stop] Closed MT log file\n";
            }
            mt_log_file.open(current_project_path + "/" + memory_transactions_filename, std::ios::out | std::ios::trunc);
            if (!mt_log_file) {
                std::cerr << "[Clear] Failed to truncate MT log file.\n";
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
    // Line 1: Total packets
    ImGui::Text("Total packets %llu : %llu valid | %llu invalid",
        total_packets, valid_packets, invalid_packets);

    // Line 2: Valid packets with green "Writes"
    ImGui::Text("Valid packets %llu :", valid_packets);
    ImGui::SameLine();
    ImGui::Text("Reads %llu |", read_count);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));  // Green
    ImGui::Text("Writes %llu", write_count);
    ImGui::PopStyleColor();

    // Line 3: Local vs Foreign with purple "Foreign"
    ImGui::Text("Local packets: %llu |", valid_packets - ignored_chip_packets);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 1.0f, 1.0f));  // Purple
    ImGui::Text("Foreign packets: %llu", ignored_chip_packets);
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::BeginChild("ScrollRegion", ImVec2(0, 200), true);

    static const ImVec4 red_shades[10] = {
        ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ImVec4(1.0f, 0.2f, 0.2f, 1.0f), ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
        ImVec4(1.0f, 0.4f, 0.4f, 1.0f), ImVec4(1.0f, 0.5f, 0.5f, 1.0f), ImVec4(1.0f, 0.6f, 0.6f, 1.0f),
        ImVec4(1.0f, 0.7f, 0.7f, 1.0f), ImVec4(1.0f, 0.8f, 0.8f, 1.0f), ImVec4(1.0f, 0.9f, 0.9f, 1.0f),
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
    };



    {
        std::lock_guard<std::mutex> lock(packet_mutex);

        size_t total = full_transaction_log.size();
        int shown = 0;
        for (int i = static_cast<int>(total) - 1; i >= 0 && shown < 10; --i) {
            if (full_transaction_log[i].counter <= current_time_counter) {
                const Packet& pkt = full_transaction_log[i];

                int recent_index = -1;
                for (int j = static_cast<int>(recent_addresses.size()) - 1; j >= 0; --j) {
                    if (recent_addresses[j] == pkt.address) {
                        recent_index = j;
                        break;
                    }
                }

                // Adjust index if fewer than 10 recent addresses
                if (recent_index != -1 && recent_addresses.size() < 10) {
                    recent_index += (10 - static_cast<int>(recent_addresses.size()));
                }


                // Format base line
                char line[128];
                snprintf(line, sizeof(line), "%llu | %s | 0x%X | 0x%X%s",
                    pkt.counter,
                    pkt.op.c_str(),
                    pkt.address,
                    pkt.value,
                    pkt.foreign_chip ? " | foreign" : ""
                );

                if (recent_index != -1) {
                    ImVec4 color = red_shades[9 - std::min(recent_index, 9)];
                    ImGui::TextColored(color, "%s", line);
                }
                else {
                    ImGui::Text("%s", line);
                }

                ++shown;
            }
        }
    }



    ImGui::EndChild();
    ImGui::End();

    render_read_write_memory();


    ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_Once);
    ImGui::Begin("Chip Configuration");

    ImGui::InputText("Chip Name", &chip_name[0], 64);
    ImGui::RadioButton("28 pins", &selected_pin_count, 28); ImGui::SameLine();
    ImGui::RadioButton("30 pins", &selected_pin_count, 30); ImGui::SameLine();
    ImGui::RadioButton("32 pins", &selected_pin_count, 32);
    ImGui::Separator();

    // New Input: Onboard XTAL frequency in MHz
    ImGui::Text("Onboard XTAL Frequency:");
    ImGui::InputInt("MHz", &xtal_freq_mhz);
    if (xtal_freq_mhz < 0.1f) xtal_freq_mhz = 0.1f;

    // New Option: What CPU should do if not streaming
    //static int cpu_idle_behavior = 0; // 0 = halt, 1 = run at XTAL
    ImGui::Text("If not streaming CPU should:");
    ImGui::RadioButton("not run (0 Hz clk)", &cpu_idle_behavior, 0); ImGui::SameLine();
    ImGui::RadioButton("run at XTAL frequency", &cpu_idle_behavior, 1);

    ImGui::Text("Streaming Frequency (max. 70kHz) :");
    ImGui::InputInt("kHz", &selected_freq_khz);
    if (selected_freq_khz < 1) selected_freq_khz = 1;

    ImGui::Text("Capture Mode:");
    ImGui::RadioButton("Interrupt", &selected_mode, 0); ImGui::SameLine();
    ImGui::RadioButton("Continuous", &selected_mode, 1);

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
    ImGui::Dummy(ImVec2(0, pin_start_offset * (dropdown_height + 5)));

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

    ImGui::SetNextWindowSize(ImVec2(500, 250), ImGuiCond_FirstUseEver);
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


    if (breakpoint_pending_rebuild) {
        std::cout << "rebuild pending" << std::endl;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - breakpoint_trigger_timestamp
            ).count();

        if (elapsed >= 500) {
            std::cout << "elapsed" << std::endl;
            //streaming_mode_enabled = false;
            std::cout << "elapsed" << std::endl;
            teensy_streaming.store(false);
            std::cout << "elapsed" << std::endl;
            current_time_counter.store(breakpoint_trigger_time);  // 👈 set the slider position
            std::cout << "elapsed" << std::endl;
            rebuild_memory_state_up_to(breakpoint_trigger_time);
            std::cout << "elapsed" << std::endl;
            std::cout << "[Breakpoint] Rebuilt memory at counter " << breakpoint_trigger_time << "\n";
            user_overridden_slider = true; // 👈 prevent auto-follow after breakpoint
            breakpoint_triggered = false;
            breakpoint_pending_rebuild = false;
            std::cout << "elapsed" << std::endl;
        }
        std::cout << "done rebuild pending" << std::endl;
    }
}


int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    // Create or clear the temp file for memory transaction logging
    //mt_log_file.open(temp_mt_log_filename, std::ios::out | std::ios::trunc);
    std::fill(std::begin(read_memory_data), std::end(read_memory_data), 0xFF);

    SDL_Window* window = SDL_CreateWindow(
        "Teensy Serial Monitor",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1400, 600,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE  // 👈 Add this
    );

    SDL_MaximizeWindow(window);  // 👌 Works properly with resizable flag


    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    load_chip_image(renderer);
    ensure_chipconfig_directory_exists();
    load_config_from_json("chipconfigs/AT28C64B.json");

    bool quit = false;
    //while (!quit) {
    //    SDL_Event event;

    //    bool shouldRender = false;
    //    SDL_WaitEvent(&event);
    //    ImGui_ImplSDL2_ProcessEvent(&event);

    //    switch (event.type) {
    //    case SDL_QUIT:
    //        quit = true;
    //    case SDL_KEYDOWN:
    //    case SDL_KEYUP:
    //    case SDL_MOUSEBUTTONDOWN:
    //    case SDL_MOUSEBUTTONUP:
    //    case SDL_MOUSEWHEEL:
    //    case SDL_WINDOWEVENT:
    //        shouldRender = true;
    //        break;
    //    case SDL_MOUSEMOTION: {
    //        static auto last_motion_render = std::chrono::steady_clock::now();
    //        auto now = std::chrono::steady_clock::now();
    //        if (now - last_motion_render > std::chrono::milliseconds(16)) {  // ~60fps
    //            shouldRender = true;
    //            last_motion_render = now;
    //        }
    //        break;
    //    }
    //    default:
    //        break;
    //    }

    //    if (shouldRender) {
    //        ImGui_ImplSDLRenderer2_NewFrame();
    //        ImGui_ImplSDL2_NewFrame();
    //        ImGui::NewFrame();

    //        render_gui();  // ✅ only call when inside a valid frame


    //        ImGui::Render();
    //        SDL_RenderClear(renderer);
    //        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    //        SDL_RenderPresent(renderer);
    //    }

    //    std::this_thread::sleep_for(std::chrono::milliseconds(16));

    //}

    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                quit = true;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (show_project_dialog && !project_selected) {
            ImGui::OpenPopup("Select Project");
        }

        if (ImGui::BeginPopupModal("Select Project", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char input_buf[256] = "";
            static std::vector<std::string> project_dirs;
            static bool loaded_dirs = false;
            static int selected_index = -1;

            // Reload project list once
            if (!loaded_dirs) {
                project_dirs.clear();
                WIN32_FIND_DATAA ffd;
                HANDLE hFind = FindFirstFileA("projects\\*", &ffd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                            strcmp(ffd.cFileName, ".") != 0 &&
                            strcmp(ffd.cFileName, "..") != 0) {
                            project_dirs.push_back(ffd.cFileName);
                        }
                    } while (FindNextFileA(hFind, &ffd));
                    FindClose(hFind);
                }
                loaded_dirs = true;
                selected_index = -1;
            }

            // New project input + Create
            ImGui::Text("Create New Project:");
            ImGui::PushItemWidth(250);
            ImGui::InputText("##newproject", input_buf, IM_ARRAYSIZE(input_buf));
            ImGui::SameLine();
            if (ImGui::Button("Create")) {
                project_name = input_buf;
                if (!project_name.empty()) {
                    save_project(project_name);
                    load_project(project_name);
                    project_selected = true;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::Separator();
            ImGui::Text("Double-click to load an existing project:");

            ImGui::BeginChild("ProjectList", ImVec2(400, 200), true);
            for (int i = 0; i < project_dirs.size(); ++i) {
                bool is_selected = (selected_index == i);
                if (ImGui::Selectable(project_dirs[i].c_str(), is_selected)) {
                    selected_index = i;
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    load_project(project_dirs[i]);
                    project_selected = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndChild();

            ImGui::Separator();

            // Open and Delete Buttons
            bool has_selection = selected_index >= 0 && selected_index < project_dirs.size();
            if (ImGui::Button("Open") && has_selection) {
                load_project(project_dirs[selected_index]);
                project_selected = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && has_selection) {
                std::string to_delete = "projects/" + project_dirs[selected_index];
                fs::remove_all(to_delete);
                loaded_dirs = false;  // Reload project list
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                quit = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }


        if (project_selected) {
            render_gui();  // your actual UI here
        }

        ImGui::Render();
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
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
