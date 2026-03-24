This project uses C++ 26 and has static reflection enabled.
Always use ranges when dealing with sorting, find, and any other ranges compatible necessity.

## Coding Standard
Follow this conding standard:
Always use snake case.
all global variables should start with g_
all private member variables should start with m_
all function parameters should start with _
if a if or a for loop only has a single line never use brackets
always put else and else if in the next line never the same as the brackets

example:

class my_class {
private:
    int m_var = 0;
public:
    float delta = 12;

template<typename T>
int add(T _other) {
    if (m_var == 9)
        m_var = -1
    else 
        m_var++;
    return m_var;
}

void get_delta() {
    return delta;
}
}