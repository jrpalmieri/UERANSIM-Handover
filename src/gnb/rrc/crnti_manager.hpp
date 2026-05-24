//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <stack>
#include <vector>

namespace nr::gnb
{

// Stack-based O(1) C-RNTI allocator.  Pre-populates the full valid range
// (1–65529) on construction; allocate() pops the top, release() pushes back.
class CrntiManager
{
    static constexpr int kMin = 1;
    static constexpr int kMax = 65529;

    std::stack<int, std::vector<int>> m_free;

  public:
    CrntiManager()
    {
        std::vector<int> v;
        v.reserve(kMax - kMin + 1);
        for (int i = kMax; i >= kMin; --i)
            v.push_back(i);
        m_free = std::stack<int, std::vector<int>>(std::move(v));
    }

    // Returns 0 if the pool is exhausted (should never happen in practice).
    int allocate()
    {
        if (m_free.empty())
            return 0;
        int crnti = m_free.top();
        m_free.pop();
        return crnti;
    }

    void release(int crnti)
    {
        if (crnti >= kMin && crnti <= kMax)
            m_free.push(crnti);
    }
};

} // namespace nr::gnb
