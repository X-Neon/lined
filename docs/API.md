# line_reader

```cpp
class line_reader
{
    using completion_callback_t = std::vector<std::string>(std::string_view);
    using hint_callback_t = std::string(std::string_view);
    using color_callback_t = void(std::string_view, style_iterator);

    line_reader(options opt = default_options);
    
    line getline(const styled_string& prompt);
    line getline(std::string_view prompt);

    std::optional<line> getline_nonblocking(const styled_string& prompt);
    std::optional<line> getline_nonblocking(std::string_view prompt);

    void cancel();

    void clear_screen();

    void mask();
    void unmask();

    void add_history(std::string_view str);
    void save_history(const std::string& path);
    void load_history(const std::string& path);

    void set_completion(std::function<completion_callback_t> callback);
    void set_hint(std::function<hint_callback_t> callback);
    void set_colorization(std::function<color_callback_t> callback);

    void disable_output();
    void enable_output();
};
```

The main class for reading user input.

# scoped_disable

```cpp
class scoped_disable
{
    scoped_disable(line_reader& reader);
    scoped_disable(scoped_disable&& other) noexcept;
    scoped_disable& operator=(scoped_disable&& other) noexcept;
    ~scoped_disable();
};
```

An RAII type which calls `line_reader::disable_output` when constructed, and `line_reader::enable_output` when destructed.

# options

```cpp
struct options 
{
    int in_fd;
    int out_fd;
    int history_size;
    bool auto_history;
    style hint_style;
};

constexpr options default_options{STDIN_FILENO, STDOUT_FILENO, 100, true, {.fg = color::gray()}};
```

* `in_fd` - The input file descriptor
* `out_fd` - The output file descriptor
* `history_size` - The maximum number of history entries
* `auto_history` - When enabled, entered lines are automatically added to the history
* `hint_style` - The text style to apply to hints

# styled_string

```cpp
class styled_string
{
    styled_string();
    styled_string(std::string_view str);

    styled_string& operator<<(std::string_view str);
    styled_string& operator<<(style s);
};
```

A string that can have text styles applied to it. Used to set the prompt for active lines.

# style

```cpp
class color
{
    constexpr color();
    constexpr color(uint8_t color_code);
    constexpr color(uint8_t r, uint8_t g, uint8_t b);

    constexpr bool operator==(const color& other) const;
    constexpr bool operator!=(const color& other) const;

    constexpr static color black();
    constexpr static color red();
    constexpr static color green();
    constexpr static color yellow();
    constexpr static color blue();
    constexpr static color magenta();
    constexpr static color cyan();
    constexpr static color white();
    constexpr static color gray();
    constexpr static color bright_red();
    constexpr static color bright_green();
    constexpr static color bright_yellow();
    constexpr static color bright_blue();
    constexpr static color bright_magenta();
    constexpr static color bright_cyan();
    constexpr static color bright_white();
};

struct style
{
    bool bold;
    color fg;
    color bg;
};
```

A text `style` is a combination of a foreground color, a background color and an optional bold mode. Colors can be specified using 8-bit color codes, 24-bit RGB values, or by using a named color.

# style_iterator

```cpp
class style_iterator
{
    style_iterator& operator*();
    style_iterator operator[](int n) const;
    
    style_iterator& operator=(const style& s);

    style_iterator& operator++();
    style_iterator operator++(int);

    style_iterator& operator--();
    style_iterator operator--(int);

    style_iterator& operator+=(int n);
    style_iterator& operator-=(int n);

    style_iterator operator+(int n) const;
    style_iterator operator-(int n) const;
    
    bool operator==(const style_iterator& other) const;
    bool operator!=(const style_iterator& other) const;
    bool operator<(const style_iterator& other) const;
    bool operator>(const style_iterator& other) const;
    bool operator<=(const style_iterator& other) const;
    bool operator>=(const style_iterator& other) const;
};
```

A `style_iterator` allows you to apply styling to the input, e.g. for syntax highlighting. A `style_iterator` is a random access output iterator.

# line

```cpp
enum class line_error
{
    ctrl_c,
    ctrl_d,
    cancelled,
    syscall
};

class line
{
    line(std::string str);
    line(line_error e);

    std::string& operator*();
    const std::string& operator*() const;

    std::string* operator->();
    const std::string* operator->() const;

    bool has_value() const;
    operator bool() const;

    std::string& value();
    const std::string& value() const;

    std::string value_or(std::string_view default_value) const;

    line_error& error();
    const line_error& error() const;

};
```

A `line` is the output of a `getline` or `getline_nonblocking` call. It acts like a `std::expected<std::string, line_error>`, in that it either contains the input text (`std::string`) or an error code (`line_error`). The possible error values are:

* `ctrl_c` - Ctrl+C was entered
* `ctrl_d` - Ctrl+D was entered on an empty line
* `cancelled` - `line_reader::cancel` was called with an active input line
* `syscall` - The `read` system call which gets user input did not complete sucessfully