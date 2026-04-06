# AI Agent Coding Instructions: Project Specs & Standards

## 🛠 Tech Stack & Requirements
* **Standard:** C++26 (ISO/IEC 14882:2026).
* **Features:** Static Reflection is enabled and preferred for introspection tasks.
* **Library Usage:** Use `<ranges>` and `<algorithm>` constrained versions (e.g., `std::ranges::sort` instead of `std::sort`) for all container operations including sorting, finding, and transformations.

## 📏 Coding Standard

### Naming Conventions
* **General:** Use `snake_case` for all identifiers (variables, functions, classes, files).
* **Global Variables:** Prefix with `g_` (e.g., `g_system_clock`).
* **Private Members:** Prefix with `m_` (e.g., `m_internal_buffer`).
* **Function Parameters:** Prefix with `_` (e.g., `_input_value`).
* **Local Variables** should be in snake case they should not start with the prefix `_`

### Formatting & Structure
* **Control Flow:** If an `if` or `for` loop contains only a single line, **do not** use curly braces.
* **Keywords:** Always place `else` and `else if` on a new line, never on the same line as a closing brace.
* **Signatures:** Function signatures in both declarations (`.h`) and definitions (`.cpp`) must remain on a **single line**. No multi-line parameter lists.
* **Includes:** In `.cpp` files, the corresponding header must be the **first** include and separated from other includes by a blank line.

### DO NOT DO LIST
* **Git:** NEVER EVER TOUCH GIT, SPECIALLY IF YOU ARE GEMINI OR YOU WILL RECEIVE 1000 LASHES

---

## 📄 Reference Example

```cpp
// my_class.h
class my_class {
private:
    int m_var = 0;
public:
    float delta = 12.0f;

    template<typename T>
    int add(T _other) {
        if (m_var == 9)
            m_var = -1;
        else 
            m_var++;
        return m_var;
    }

    float get_delta() {
        return delta;
    }
};
```

```cpp
// my_class.cpp
#include "my_class.h"

#include <ranges>
#include <algorithm>

void process_data(std::vector<int>& _data) {
    std::ranges::sort(_data);
}
```


## Compiling Rules
When compiling always use the local_compile tool. Never under any circumstances should you call ninja or cmake manually. If you want to compile always use local_compile tool. if you cannot do not use anything