/*
 * SessionFind.cpp
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

#include "SessionFind.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <core/Exec.hpp>
#include <core/StringUtils.hpp>
#include <core/system/Environment.hpp>
#include <core/system/Process.hpp>
#include <core/system/ShellUtils.hpp>

#include <session/SessionModuleContext.hpp>
#include <session/projects/SessionProjects.hpp>

using namespace core;

namespace session {
namespace modules {   
namespace find {

namespace {

   class GrepOperation : public boost::enable_shared_from_this<GrepOperation>
   {
   public:
      static boost::shared_ptr<GrepOperation> create(const FilePath& tempFile)
      {
         return boost::shared_ptr<GrepOperation>(new GrepOperation(tempFile));
      }

   private:
      GrepOperation(const FilePath& tempFile)
         : stopped_(false), tempFile_(tempFile)
      {
         handle_ = core::system::generateUuid(false);
      }

   public:
      std::string handle() const
      {
         return handle_;
      }

      core::system::ProcessCallbacks createProcessCallbacks()
      {
         core::system::ProcessCallbacks callbacks;
         callbacks.onContinue = boost::bind(&GrepOperation::onContinue,
                                            shared_from_this(),
                                            _1);
         callbacks.onStdout = boost::bind(&GrepOperation::onStdout,
                                          shared_from_this(),
                                          _1, _2);
         callbacks.onStderr = boost::bind(&GrepOperation::onStderr,
                                          shared_from_this(),
                                          _1, _2);
         callbacks.onExit = boost::bind(&GrepOperation::onExit,
                                        shared_from_this(),
                                        _1);
         return callbacks;
      }

      void stop()
      {
         stopped_ = true;
      }

   private:
      bool onContinue(const core::system::ProcessOperations& ops) const
      {
         return !stopped_;
      }

      void onStdout(const core::system::ProcessOperations& ops, const std::string& data)
      {
         json::Array files;
         json::Array lineNums;
         json::Array contents;

         stdOutBuf_.append(data);
         size_t nextLineStart = 0;
         size_t pos = -1;
         while (std::string::npos != (pos = stdOutBuf_.find('\n', pos + 1)))
         {
            std::string line = stdOutBuf_.substr(nextLineStart, pos - nextLineStart);
            nextLineStart = pos + 1;

            boost::smatch match;
            if (boost::regex_match(line, match, boost::regex("^([^:]+):(\\d+):(.*)")))
            {
               std::string file = match[1];
               int lineNum = safe_convert::stringTo<int>(std::string(match[2]), -1);
               std::string lineContents = match[3];
               boost::algorithm::trim(lineContents);

               files.push_back(file);
               lineNums.push_back(lineNum);
               contents.push_back(lineContents);
            }
         }

         if (nextLineStart)
         {
            stdOutBuf_.erase(0, nextLineStart);
         }

         json::Object result;
         result["handle"] = handle();
         json::Object results;
         results["file"] = files;
         results["line"] = lineNums;
         results["lineValue"] = contents;
         result["results"] = results;

         module_context::enqueClientEvent(
               ClientEvent(client_events::kFindResult, result));
      }

      void onStderr(const core::system::ProcessOperations& ops, const std::string& data)
      {
         LOG_ERROR_MESSAGE("grep: " + data);
      }

      void onExit(int exitCode)
      {
         if (!tempFile_.empty())
            tempFile_.removeIfExists();
      }

      bool stopped_;
      FilePath tempFile_;
      std::string stdOutBuf_;
      std::string handle_;
   };

} // namespace

core::Error beginFind(const json::JsonRpcRequest& request,
                      json::JsonRpcResponse* pResponse)
{
   std::string searchString;
   bool asRegex, ignoreCase;
   std::string directory;
   std::string filePattern;

   Error error = json::readParams(request.params,
                                  &searchString,
                                  &asRegex,
                                  &ignoreCase,
                                  &directory,
                                  &filePattern);
   if (error)
      return error;

   core::system::ProcessOptions options;
   options.workingDir = module_context::resolveAliasedPath(directory);

   // TODO: Encode the pattern using the project encoding

   // Put the grep pattern in a file
   FilePath tempFile = module_context::tempFile("rs_grep", "txt");
   boost::shared_ptr<std::ostream> pStream;
   error = tempFile.open_w(&pStream);
   if (error)
      return error;
   *pStream << searchString << std::endl;
   pStream.reset(); // release file handle

   boost::shared_ptr<GrepOperation> ptrGrepOp = GrepOperation::create(tempFile);
   core::system::ProcessCallbacks callbacks =
                                       ptrGrepOp->createProcessCallbacks();

   shell_utils::ShellCommand cmd("grep");
   cmd << "-rHn" << "--binary-files=without-match" << "--devices=skip";

   if (ignoreCase)
      cmd << "-i";

   // Use -f to pass pattern via file, so we don't have to worry about
   // escaping double quotes, etc.
   cmd << "-f";
   cmd << tempFile;
   if (!asRegex)
      cmd << "-F";

   cmd << "--";
   if (filePattern.empty())
      cmd << shell_utils::EscapeFilesOnly << "*" << shell_utils::EscapeAll;
   else
      cmd << filePattern;

   error = module_context::processSupervisor().runCommand(cmd,
                                                          options,
                                                          callbacks);
   if (error)
      return error;

   pResponse->setResult(ptrGrepOp->handle());

   return Success();
}

core::Error stopFind(const json::JsonRpcRequest& request,
                     json::JsonRpcResponse* pResponse)
{
   std::string handle;

   return Success();
}

core::Error initialize()
{
   using namespace session::module_context;

   // install handlers
   using boost::bind;
   ExecBlock initBlock ;
   initBlock.addFunctions()
      (bind(registerRpcMethod, "begin_find", beginFind))
      (bind(registerRpcMethod, "stop_find", stopFind));
   return initBlock.execute();
}

} // namespace find
} // namespace modules
} // namespace session
