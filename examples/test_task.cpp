/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: test.cpp
 *     Created: 2018-02-04 17:40
 * Description:
 * ===================================================
 */
#include <thread>
#include <vector>
#include <string>
#include <assert.h>
#include "kx_task.h"

using namespace kx;

class tester {
public:
    ~tester() {
        m_ch.pop();
    }

    void start(const char *name, int start, int num) {
        if (name)
            m_name = name;
        m_last = start;
        int N = start+num;
        std::thread t([this, start, N] {
            for (int i=start; i<N; i++) {
                RunTask([this, i, N]{
                    if (!m_name.empty()) {
                        assert(i == m_last++);
                    }
                    if (i == N-1) {
                        m_ch.push(1);
                    }
                }, m_name.c_str());
            }
        });
        t.detach();
    }

private:
    std::string m_name;
    Chan<int> m_ch;
    int m_last;
};

void foo(Chan<int> &ch, int n) {
    static int prev = -1;
    if (n >= 10) {
        ch.push(1);
        return;
    }
    if (prev == -1)
        prev = n;
    else
        assert(n == ++prev);

    RunTask([=, &ch]{ foo(ch, n+1);}, 100);
}

int main(int argc, char *argv[])
{
    std::vector<tester> testers(20);

    Chan<int> ch;
    RunTask([&] { foo(ch, 0); }, 100);

    for (size_t i=0; i<testers.size(); i++) {
        char name[32];
        sprintf(name, "tester%d", i);
        testers[i].start(name, 0, 1000);
    }

    ch.pop();
    printf("all test passed\n");

    return 0;
}

