#pragma once
#include <string>
#include <vector>
#include "ast.hpp"

namespace dessa {

// The whole tree as text, one node per line (`dessa ast`): its position, its
// kind and its own values, with its children indented under it. The parser
// written in Dessa (selfhost/) prints exactly the same, and
// selfhost/check.py compares the two.
std::string dumpAst(const std::vector<StmtPtr>& program, const std::vector<StmtPtr>& templates);

} // namespace dessa
