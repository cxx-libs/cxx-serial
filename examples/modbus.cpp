#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <thread>
#include <chrono>
#include <iomanip>
#include <serial_cpp/serial.h>

using namespace serial_cpp;

/// ---------------- MODBUS CRC16 ----------------
uint16_t modbus_crc16(const std::vector<uint8_t>& data)
{
   std::uint16_t crc = 0xFFFF;
    for (auto b : data)
    {
        crc ^= b;
        for (int i = 0; i < 8; i++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/// ---------------- BUILD MODBUS REQUEST ----------------
std::vector<uint8_t> build_request(uint8_t slave, uint16_t reg, uint16_t qty)
{
    std::vector<uint8_t> req;
    req.push_back(slave);
    req.push_back(0x03); // read holding registers
    req.push_back(reg >> 8);
    req.push_back(reg & 0xFF);
    req.push_back(qty >> 8);
    req.push_back(qty & 0xFF);

    uint16_t crc = modbus_crc16(req);
    req.push_back(crc & 0xFF);
    req.push_back(crc >> 8);

    return req;
}

/// ---------------- READ RESPONSE ----------------
bool read_response(serial_cpp::Serial& serial, std::vector<uint8_t>& buf)
{
    buf.clear();
    uint8_t byte;
    size_t expected_size = 0;

    auto start = std::chrono::steady_clock::now();
    while (true)
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > 2000)
            return false; // timeout

        size_t n = serial.read(&byte, 1);
        if (n == 1)
        {
            buf.push_back(byte);

            if (buf.size() == 3)
            {
                uint8_t byte_count = buf[2];
                expected_size = 3 + byte_count + 2;
            }

            if (expected_size > 0 && buf.size() >= expected_size)
                return true; // full response received
        }
    }
}

/// ---------------- READ REGISTERS (WITH CRC CHECK) ----------------
bool read_registers(serial_cpp::Serial &serial, uint8_t slave, uint16_t reg, uint16_t qty, std::vector<uint8_t>& resp)
{
    resp.clear();
    auto req = build_request(slave, reg, qty);
    serial.write(req);

    if (!read_response(serial, resp))
    {
        std::cerr << "[WARN] Timeout reading register 0x" << std::hex << reg << "\n";
        return false;
    }

    // Validate CRC
    if (resp.size() < 5)
    {
        std::cerr << "[WARN] Response too short for register 0x" << std::hex << reg << "\n";
        return false;
    }

    uint16_t crc_calc = modbus_crc16({resp.begin(), resp.end() - 2});
    uint16_t crc_resp = resp[resp.size()-2] | (resp[resp.size()-1] << 8);
    if (crc_calc != crc_resp)
    {
        std::cerr << "[WARN] CRC mismatch for register 0x" << std::hex << reg << "\n";
        return false;
    }

    if (resp[1] & 0x80)
    {
        std::cerr << "[WARN] Modbus exception for register 0x" << std::hex << reg << "\n";
        return false;
    }

    return true;
}

/// ---------------- PARSE 32-BIT FLOAT ----------------
float parse_float32(const std::vector<uint8_t> &resp)
{
    if (resp.size() < 7) return 0.0f;
    uint32_t raw =
        (uint32_t(resp[3]) << 24) |
        (uint32_t(resp[4]) << 16) |
        (uint32_t(resp[5]) << 8) |
        (uint32_t(resp[6]));
    float value;
    std::memcpy(&value, &raw, sizeof(float));
    return value;
}

/// ---------------- PARSE STRING ----------------
std::string parse_string(const std::vector<uint8_t>& resp)
{
    if (resp.size() < 5) return "";
    uint8_t byte_count = resp[2];
    std::string out;
    out.reserve(byte_count);

    for (size_t i = 0; i < byte_count; i++)
    {
        char c = resp[3 + i];
        if (c >= 32 && c <= 126)
            out.push_back(c);
    }
    while (!out.empty() && (out.back() < 32 || out.back() > 126))
        out.pop_back();

    return out;
}

/// ---------------- MAIN ----------------
int main()
{
    serial_cpp::Serial serial(
        "/dev/ttyUSB0",
        9600,
        serial_cpp::Timeout::simpleTimeout(1000),
        serial_cpp::eightbits,
        serial_cpp::parity_none,
        serial_cpp::stopbits_two,
        serial_cpp::flowcontrol_none
    );

    if (!serial.isOpen())
        serial.open();

    std::cout << "Serial opened\n";

    uint8_t slave = 247;
    std::vector<uint8_t> resp;

    while (true)
    {
        serial.flushInput();

        float gas_flow = 0, temperature = 0, totaliser = 0;
        std::string measuring_unit;

        if (read_registers(serial, slave, 0x0000, 2, resp))
            gas_flow = parse_float32(resp);

        if (read_registers(serial, slave, 0x0002, 2, resp))
            temperature = parse_float32(resp);

        if (read_registers(serial, slave, 0x0004, 2, resp))
            totaliser = parse_float32(resp);

        if (read_registers(serial, slave, 0x6046, 4, resp))
            measuring_unit = parse_string(resp);

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "Gas Flow: " << gas_flow << "\n";
        std::cout << "Temperature: " << temperature << "\n";
        std::cout << "Totaliser: " << totaliser << "\n";
        std::cout << "Measuring Unit: " << measuring_unit << "\n";
        std::cout << "-------------------------------\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}