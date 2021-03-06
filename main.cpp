#include <getopt.h>
#include <ncurses.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {
const int end_of_transmission = 4;
const int escape = 27;

namespace color {
    bool use_color = true;
    enum class type {
        label = 1,
        value,
        ok,
        warn,
        bad
    };
}

void set_color(color::type color)
{
    if (color::use_color) {
        attron(COLOR_PAIR(static_cast<int>(color)));
    }
}

void remove_color(color::type color)
{
    if (color::use_color) {
        attroff(COLOR_PAIR(static_cast<int>(color)));
    }
}

void print_string(color::type color, const std::string &str, int attr = 0)
{
    attron(attr);
    set_color(color);
    addstr(str.c_str());
    remove_color(color);
    attroff(attr);
}

class device {
public:
    device(std::string_view path)
        : m_path(path)
    {
        m_vram = std::stoull(read_file("mem_info_vram_total"));
        m_vram_str = '/' + std::to_string(m_vram / (1024ull * 1024ull)) + "MiB";

        m_gtt = std::stoull(read_file("mem_info_gtt_total"));
        m_gtt_str = '/' + std::to_string(m_gtt / (1024ull * 1024ull)) + "MiB";

        m_vis_vram = std::stoull(read_file("mem_info_vis_vram_total"));
        m_vis_vram_str = '/' + std::to_string(m_vis_vram / (1024ull * 1024ull)) + "MiB";

        m_power_min = std::stoull(read_file("hwmon/hwmon1/power1_cap_min"));
        m_power_max = std::stoull(read_file("hwmon/hwmon1/power1_cap_max"));

        m_temp_crit = std::stoull(read_file("hwmon/hwmon1/temp1_crit"));

        m_fan_min = std::stoull(read_file("hwmon/hwmon1/fan1_min"));
        m_fan_max = std::stoull(read_file("hwmon/hwmon1/fan1_max"));
    }

    std::pair<std::string, double> busy() const
    {
        auto pc = read_file("gpu_busy_percent");
        return std::make_pair(pc + '%', std::stod(pc) * 0.01);
    }

    std::pair<std::string, double> vram() const
    {
        auto used = read_file("mem_info_vram_used");
        auto u = std::stoull(used);
        auto pc = static_cast<double>(u) / static_cast<double>(m_vram);
        u /= 1024ull * 1024ull;

        used = std::to_string(u) + m_vram_str;
        return std::make_pair(used, pc);
    }

    std::pair<std::string, double> gtt() const
    {
        auto used = read_file("mem_info_gtt_used");
        auto u = std::stoull(used);
        auto pc = static_cast<double>(u) / static_cast<double>(m_gtt);
        u /= 1024ull * 1024ull;

        used = std::to_string(u) + m_gtt_str;
        return std::make_pair(used, pc);
    }

    std::pair<std::string, double> vis_vram() const
    {
        auto used = read_file("mem_info_vis_vram_used");
        auto u = std::stoull(used);
        auto pc = static_cast<double>(u) / static_cast<double>(m_vis_vram);
        u /= 1024ull * 1024ull;

        used = std::to_string(u) + m_vis_vram_str;
        return std::make_pair(used, pc);
    }

    std::pair<std::string, double> power() const
    {
        auto pwr = read_file("hwmon/hwmon1/power1_average");
        auto p = std::stoull(pwr);
        auto range = static_cast<double>(m_power_max - m_power_min);
        auto pc = static_cast<double>(p - m_power_min) / range;

        return std::make_pair(std::to_string(p / 1000000ull) + 'W', pc);
    }

    std::pair<std::string, double> temperature() const
    {
        auto temp = read_file("hwmon/hwmon1/temp1_input");
        auto t = std::stoull(temp);
        auto pc = static_cast<double>(t) / static_cast<double>(m_temp_crit);

        return std::make_pair(std::to_string(t / 1000ull) + 'C', pc);
    }

    std::pair<std::string, double> fan() const
    {
        auto f = read_file("hwmon/hwmon1/fan1_input");
        auto range = static_cast<double>(m_fan_max - m_fan_min);
        auto pc = static_cast<double>(std::stod(f) - m_fan_min) / range;

        return std::make_pair(f + "RPM", pc);
    }

    std::string voltage() const
    {
        return read_file("hwmon/hwmon1/in0_input") + "mV";
    }

    std::string gfx_clock() const
    {
        auto freq = read_file("hwmon/hwmon1/freq1_input");
        auto f = std::stoull(freq);
        f /= 1000000ull;
        return std::to_string(f) + "MHz";
    }

    std::string mem_clock() const
    {
        auto freq = read_file("hwmon/hwmon1/freq2_input");
        auto f = std::stoull(freq);
        f /= 1000000ull;
        return std::to_string(f) + "MHz";
    }

    std::string link_speed() const
    {
        return read_file("current_link_speed");
    }

    std::string link_width() const
    {
        return 'x' + read_file("current_link_width");
    }

private:
    std::string read_file(const std::string_view path) const
    {
        auto file = m_path;
        file.append(path);

        std::ifstream input(file);
        if (!input.is_open()) {
            return "0";
        }

        std::string ret;
        std::getline(input, ret);
        return ret;
    }

    std::string m_path;
    std::string m_vram_str;
    std::string m_gtt_str;
    std::string m_vis_vram_str;

    using ull = unsigned long long;

    ull m_vram;
    ull m_gtt;
    ull m_vis_vram;
    ull m_power_min;
    ull m_power_max;
    ull m_temp_crit;
    ull m_fan_min;
    ull m_fan_max;
};

void draw_bar(int row, int col, int width, double pc, const std::string &str)
{
    move(row, col);
    clrtoeol();

    pc = std::clamp(pc, 0.0, 1.0);
    width -= 2 + static_cast<int>(str.size());

    auto bars = static_cast<int>(width * pc);
    if (bars < 0) {
        return;
    }

    attron(A_BOLD);
    addch('[');
    attroff(A_BOLD);

    auto bar_color =
        pc < 0.33 ? color::type::ok :
        pc < 0.67 ? color::type::warn :
        color::type::bad;

    std::string bar(static_cast<size_t>(bars), '|');
    print_string(bar_color, bar);

    move(row, col + width + 1);

    attron(A_BOLD);
    print_string(color::type::value, str);
    addch(']');
    attroff(A_BOLD);
}

const int vpad = 1;
const int hpad = 2;

namespace info {
enum {
    busy,
    vram,
    gtt,
    cpu_vis,
    power,
    temperature,
    fan,
    voltage,
    gfx_clock,
    mem_clock,
    link_speed,
    link_width,

    row_count
};

#define INFO(x) {#x, x}

const std::unordered_map<std::string_view, unsigned> info_map = {
    INFO(busy),
    INFO(vram),
    INFO(gtt),
    INFO(cpu_vis),
    INFO(power),
    INFO(temperature),
    INFO(fan),
    INFO(voltage),
    INFO(gfx_clock),
    INFO(mem_clock),
    INFO(link_speed),
    INFO(link_width)
};
}

void disable_option(std::vector<bool> &enabled_rows, std::string_view option)
{
    auto itr = info::info_map.find(option);
    if (itr != info::info_map.cend()) {
        enabled_rows[itr->second] = false;
    }
}

void disable_options(std::vector<bool> &enabled_rows, std::string_view options)
{
    size_t idx = 0;
    size_t prev_idx = 0;
    while ((idx = options.find(',', prev_idx)) != std::string_view::npos) {
        auto opt = options.substr(prev_idx, idx - prev_idx);
        disable_option(enabled_rows, opt);
        prev_idx = idx+1;
    }

    auto opt = options.substr(prev_idx);
    disable_option(enabled_rows, opt);
}

void draw_labels(const std::vector<bool> &enabled_rows)
{
    int row = vpad-1;

    set_color(color::type::label);
    if (enabled_rows[info::busy]) mvaddstr(++row, hpad, "GPU busy:");
    if (enabled_rows[info::vram]) mvaddstr(++row, hpad, "GPU vram:");
    if (enabled_rows[info::gtt]) mvaddstr(++row, hpad, "GTT:");
    if (enabled_rows[info::cpu_vis]) mvaddstr(++row, hpad, "CPU Vis:");
    if (enabled_rows[info::power]) mvaddstr(++row, hpad, "Power draw:");
    if (enabled_rows[info::temperature]) mvaddstr(++row, hpad, "Temperature:");
    if (enabled_rows[info::fan]) mvaddstr(++row, hpad, "Fan speed:");
    if (enabled_rows[info::voltage]) mvaddstr(++row, hpad, "Voltage:");
    if (enabled_rows[info::gfx_clock]) mvaddstr(++row, hpad, "GFX clock:");
    if (enabled_rows[info::mem_clock]) mvaddstr(++row, hpad, "Mem clock:");
    if (enabled_rows[info::link_speed]) mvaddstr(++row, hpad, "Link speed:");
    if (enabled_rows[info::link_width]) mvaddstr(++row, hpad, "Link width:");
    remove_color(color::type::label);
}

void print_help(std::string_view progName)
{
    std::cout << "Usage: " << progName << " [options]\n"
        "Released under the GNU GPLv3\n\n"
        "  -n, --no-color      disable colors\n"
        "  -u, --update=N      set automatic updates to N seconds (default 2)\n"
        "  -h, --help          display this message\n"
        "  -d, --disable=ROWS  disable each row corresponding to the comma\n"
        "                      separated list ROWS. Valid options are busy,\n"
        "                      vram, gtt, cpu_vis, power, temperature, fan,\n"
        "                      voltage, gfx_clock, mem_clock, link_speed and\n"
        "                      link_width. Other values are silently ignored.\n";
}

void handle_winch(const std::vector<bool> &enabled_rows)
{
    winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    resizeterm(w.ws_row, w.ws_col);
    clear();
    draw_labels(enabled_rows);
}

volatile sig_atomic_t should_close = 0;
volatile sig_atomic_t should_resize = 0;
void signal_handler(int sig)
{
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        should_close = 1;
        break;
    case SIGWINCH:
        should_resize = 1;
        break;
    }
}
}

int main(int argc, char **argv)
{
    const option options[] = {
        {"update", required_argument, nullptr, 'u'},
        {"no-color", no_argument, nullptr, 'n'},
        {"help", no_argument, nullptr, 'h'},
        {"disable", required_argument, nullptr, 'd'},
        {nullptr, 0, nullptr, 0}
    };

    int sleep_time = 2;

    std::vector<bool> enabled_rows(info::row_count, true);

    int c;
    while ((c = getopt_long(argc, argv, "hnu:d:", options, nullptr)) != -1) {
        switch (c) {
        case 'h':
            print_help(argv[0]);
            return EXIT_SUCCESS;
        case 'n':
            color::use_color = false;
            break;
        case 'u':
            sleep_time = std::stoi(optarg);
            break;
        case 'd':
            disable_options(enabled_rows, optarg);
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    if (std::all_of(enabled_rows.cbegin(), enabled_rows.cend(), [](auto b){return !b;})) {
        std::cout << "All rows disabled. Exiting." << std::endl;
        return EXIT_SUCCESS;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGWINCH, signal_handler);

    initscr();

    timeout(sleep_time*1000);
    noecho();
    curs_set(0);
    keypad(stdscr, true);
    clear();

    color::use_color &= has_colors();

    if (color::use_color) {
        start_color();
        use_default_colors();
        init_pair(static_cast<int>(color::type::label), COLOR_CYAN, -1);
        init_pair(static_cast<int>(color::type::value), COLOR_BLACK, -1);
        init_pair(static_cast<int>(color::type::ok), COLOR_GREEN, -1);
        init_pair(static_cast<int>(color::type::warn), COLOR_YELLOW, -1);
        init_pair(static_cast<int>(color::type::bad), COLOR_RED, -1);
    }

    draw_labels(enabled_rows);

    const auto text_len = 13 + hpad;

    const device dev("/sys/class/drm/card0/device/");

    while (!should_close) {
        if (should_resize) {
            handle_winch(enabled_rows);
            should_resize = 0;
        }

        int bar_width = COLS - text_len - hpad;
        int row = vpad-1;

        if (enabled_rows[info::busy]) {
            auto [busy, busy_pc] = dev.busy();
            draw_bar(++row, text_len, bar_width, busy_pc, busy);
        }

        if (enabled_rows[info::vram]) {
            auto [mem, mem_pc] = dev.vram();
            draw_bar(++row, text_len, bar_width, mem_pc, mem);
        }

        if (enabled_rows[info::gtt]) {
            auto [gtt, gtt_pc] = dev.gtt();
            draw_bar(++row, text_len, bar_width, gtt_pc, gtt);
        }

        if (enabled_rows[info::cpu_vis]) {
            auto [vis, vis_pc] = dev.vis_vram();
            draw_bar(++row, text_len, bar_width, vis_pc, vis);
        }

        if (enabled_rows[info::power]) {
            auto [pwr, pwr_pc] = dev.power();
            draw_bar(++row, text_len, bar_width, pwr_pc, pwr);
        }

        if (enabled_rows[info::temperature]) {
            auto [temp, temp_pc] = dev.temperature();
            draw_bar(++row, text_len, bar_width, temp_pc, temp);
        }

        if (enabled_rows[info::fan]) {
            auto [fan, fan_pc] = dev.fan();
            draw_bar(++row, text_len, bar_width, fan_pc, fan);
        }

        if (enabled_rows[info::voltage]) {
            move(++row, text_len);
            clrtoeol();
            print_string(color::type::label, dev.voltage(), A_BOLD);
        }

        if (enabled_rows[info::gfx_clock]) {
            move(++row, text_len);
            clrtoeol();
            print_string(color::type::label, dev.gfx_clock(), A_BOLD);
        }

        if (enabled_rows[info::mem_clock]) {
            move(++row, text_len);
            clrtoeol();
            print_string(color::type::label, dev.mem_clock(), A_BOLD);
        }

        if (enabled_rows[info::link_speed]) {
            move(++row, text_len);
            clrtoeol();
            print_string(color::type::label, dev.link_speed(), A_BOLD);
        }

        if (enabled_rows[info::link_width]) {
            move(++row, text_len);
            clrtoeol();
            print_string(color::type::label, dev.link_width(), A_BOLD);
        }

        refresh();

        auto key = getch();
        if (key == 'q' || key == end_of_transmission || key == escape) {
            break;
        }
    }

    endwin();

    return EXIT_SUCCESS;
}
