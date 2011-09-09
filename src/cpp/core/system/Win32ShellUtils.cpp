/*
 * Win32ShellUtils.cpp
 *
 * Copyright (C) 2009-11 by RStudio, Inc.
 *
 * This program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include <core/StringUtils.hpp>
#include <core/system/ShellUtils.hpp>

namespace core {
namespace shell_utils {

std::string escape(const std::string& arg)
{
   /* NOTE: This may be broken in cases where the arg contains special
      characters like %, ", etc. However, unlike with any sane shell, the
      Windows shell does not have a consistent way to escape out special
      characters. And what it does have is horribly under-documented. I
      thought it best to leave this simple to start and see if anything
      comes up from field testing. */
   return "\"" + arg + "\"";
}

std::string escape(const core::FilePath& path)
{
   return escape(string_utils::utf8ToSystem(path.absolutePath()));
}

std::string join(const std::string& command1, const std::string& command2)
{
   return "(" + command1 + ") & (" + command2 + ")";
}

std::string join_and(const std::string& command1, const std::string& command2)
{
   return "(" + command1 + ") && (" + command2 + ")";
}

std::string join_or(const std::string& command1, const std::string& command2)
{
   return "(" + command1 + ") || (" + command2 + ")";
}

std::string sendStdErrToStdOut(const std::string& command)
{
   return "(" + command + ") 2>&1";
}

std::string sendNullToStdIn(const std::string& command)
{
   return "(" + command + ") < NUL";
}

}
}