/*
 * MacFileMonitor.cpp
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

#include <core/system/FileMonitor.hpp>

#include <CoreServices/CoreServices.h>

#include <list>
#include <algorithm>

#include <boost/foreach.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <core/Log.hpp>
#include <core/Error.hpp>
#include <core/FileInfo.hpp>
#include <core/Thread.hpp>

#include <core/system/FileScanner.hpp>
#include <core/system/System.hpp>

namespace core {
namespace system {
namespace file_monitor {

namespace {


class FileEventContext : boost::noncopyable
{
public:
   FileEventContext() : streamRef(NULL) {}
   virtual ~FileEventContext() {}
   FSEventStreamRef streamRef;
   tree<FileInfo> fileTree;
   Callbacks::FilesChanged onFilesChanged;
};


void addEvent(FileChangeEvent::Type type,
              const FileInfo& fileInfo,
              std::vector<FileChangeEvent>* pEvents)
{
   pEvents->push_back(FileChangeEvent(type, fileInfo));
}


Error processAdded(tree<FileInfo>::iterator parentIt,
                   const FileChangeEvent& fileChange,
                   FileEventContext* pContext,
                   std::vector<FileChangeEvent>* pFileChanges)
{
   if (fileChange.fileInfo().isDirectory())
   {
      tree<FileInfo> subTree;
      Error error = scanFiles(fileChange.fileInfo(),
                              true,
                              &subTree);
      if (error)
         return error;

      // merge in the sub-tree
      tree<FileInfo>::sibling_iterator addedIter =
         pContext->fileTree.append_child(parentIt, fileChange.fileInfo());
      pContext->fileTree.insert_subtree_after(addedIter,
                                              subTree.begin());
      pContext->fileTree.erase(addedIter);

      // generate events
      std::for_each(subTree.begin(),
                    subTree.end(),
                    boost::bind(addEvent,
                                FileChangeEvent::FileAdded,
                                _1,
                                pFileChanges));
   }
   else
   {
       pContext->fileTree.append_child(parentIt, fileChange.fileInfo());
       pFileChanges->push_back(fileChange);
   }

   // sort the container after insert (so future calls to collectFileChangeEvents
   // can rely on this order)
   pContext->fileTree.sort(pContext->fileTree.begin(parentIt),
                           pContext->fileTree.end(parentIt),
                           fileInfoPathLessThan,
                           false);

   return Success();
}

void processModified(tree<FileInfo>::iterator parentIt,
                     const FileChangeEvent& fileChange,
                     FileEventContext* pContext,
                     std::vector<FileChangeEvent>* pFileChanges)
{
   tree<FileInfo>::sibling_iterator modIt =
         std::find_if(
            pContext->fileTree.begin(parentIt),
            pContext->fileTree.end(parentIt),
            boost::bind(fileInfoHasPath,
                        _1,
                        fileChange.fileInfo().absolutePath()));
   if (modIt != pContext->fileTree.end(parentIt))
      pContext->fileTree.replace(modIt, fileChange.fileInfo());

   // add it to the fileChanges
   pFileChanges->push_back(fileChange);
}

void processRemoved(tree<FileInfo>::iterator parentIt,
                    const FileChangeEvent& fileChange,
                    FileEventContext* pContext,
                    std::vector<FileChangeEvent>* pFileChanges)
{
   // find the item in the current tree
   tree<FileInfo>::sibling_iterator remIt =
         std::find(pContext->fileTree.begin(parentIt),
                   pContext->fileTree.end(parentIt),
                   fileChange.fileInfo());

   if (remIt != pContext->fileTree.end(parentIt))
   {
      // if this is folder then we need to generate recursive
      // remove events, otherwise can just add single event
      if (remIt->isDirectory())
      {
         tree<FileInfo> subTree(remIt);
         std::for_each(subTree.begin(),
                       subTree.end(),
                       boost::bind(addEvent,
                                   FileChangeEvent::FileRemoved,
                                   _1,
                                   pFileChanges));
      }
      else
      {
         pFileChanges->push_back(fileChange);
      }

      // remove it from the tree
      pContext->fileTree.erase(remIt);
   }


}


Error processFileChanges(const FileInfo& fileInfo,
                         bool recursive,
                         FileEventContext* pContext)
{
   // scan this directory into a new tree which we can compare to the old tree
   tree<FileInfo> subdirTree;
   Error error = scanFiles(fileInfo, recursive, &subdirTree);
   if (error)
      return error;

   // find this path in our fileTree
   tree<FileInfo>::iterator it = std::find(pContext->fileTree.begin(),
                                           pContext->fileTree.end(),
                                           fileInfo);
   if (it != pContext->fileTree.end())
   {
      // handle recursive vs. non-recursive scan differnetly
      if (recursive)
      {
         // check for changes on full subtree
         std::vector<FileChangeEvent> fileChanges;
         tree<FileInfo> existingSubtree(it);
         collectFileChangeEvents(existingSubtree.begin(),
                                 existingSubtree.end(),
                                 subdirTree.begin(),
                                 subdirTree.end(),
                                 &fileChanges);

         // fire events
         pContext->onFilesChanged(fileChanges);

         // wholesale replace subtree
         pContext->fileTree.insert_subtree_after(it, subdirTree.begin());
         pContext->fileTree.erase(it);
      }
      else
      {
         // scan for changes on just the children
         std::vector<FileChangeEvent> childrenFileChanges;
         collectFileChangeEvents(pContext->fileTree.begin(it),
                                 pContext->fileTree.end(it),
                                 subdirTree.begin(subdirTree.begin()),
                                 subdirTree.end(subdirTree.begin()),
                                 &childrenFileChanges);

         // build up actual file changes and mutate the tree as appropriate
         std::vector<FileChangeEvent> fileChanges;
         BOOST_FOREACH(const FileChangeEvent& fileChange, childrenFileChanges)
         {
            switch(fileChange.type())
            {
            case FileChangeEvent::FileAdded:
            {
               Error error = processAdded(it, fileChange, pContext, &fileChanges);
               if (error)
                  LOG_ERROR(error);
               break;
            }
            case FileChangeEvent::FileModified:
            {
               processModified(it, fileChange, pContext, &fileChanges);
               break;
            }
            case FileChangeEvent::FileRemoved:
            {
               processRemoved(it, fileChange, pContext, &fileChanges);
               break;
            }
            case FileChangeEvent::None:
            default:
               break;
            }
         }

         // fire events
         pContext->onFilesChanged(fileChanges);
      }
   }
   else
   {
      LOG_WARNING_MESSAGE("Unable to find treeItem for " +
                          fileInfo.absolutePath());
   }

   return Success();
}

void fileEventCallback(ConstFSEventStreamRef streamRef,
                       void *pCallbackInfo,
                       size_t numEvents,
                       void *eventPaths,
                       const FSEventStreamEventFlags eventFlags[],
                       const FSEventStreamEventId eventIds[])
{
   // get context
   FileEventContext* pContext = (FileEventContext*)pCallbackInfo;

   // bail if we don't have onFilesChanged (we wouldn't if a callback snuck
   // through to us even after we failed to fully initialize the file monitor
   // (e.g. if there was an error during file listing)
   if (!pContext->onFilesChanged)
      return;

   char **paths = (char**)eventPaths;
   for (std::size_t i=0; i<numEvents; i++)
   {
      // check for root changed (unregister)
      if (eventFlags[i] & kFSEventStreamEventFlagRootChanged)
      {
         unregisterMonitor((Handle)pContext);
         return;
      }

      // make a copy of the path and strip off trailing / if necessary
      std::string path(paths[i]);
      boost::algorithm::trim_right_if(path, boost::algorithm::is_any_of("/"));

      // get FileInfo for this directory
      FileInfo fileInfo(path, true);

      // check for need to do recursive scan
      bool recursive = eventFlags[i] & kFSEventStreamEventFlagMustScanSubDirs;

      // process changes
      Error error = processFileChanges(fileInfo, recursive, pContext);
      if (error)
         LOG_ERROR(error);
   }
}

class CFRefScope : boost::noncopyable
{
public:
   explicit CFRefScope(CFTypeRef ref)
      : ref_(ref)
   {
   }
   virtual ~CFRefScope()
   {
      try
      {
         ::CFRelease(ref_);
      }
      catch(...)
      {
      }
   }
private:
   CFTypeRef ref_;
};

void invalidateAndReleaseEventStream(FSEventStreamRef streamRef)
{
   ::FSEventStreamInvalidate(streamRef);
   ::FSEventStreamRelease(streamRef);
}

void stopInvalidateAndReleaseEventStream(FSEventStreamRef streamRef)
{
   ::FSEventStreamStop(streamRef);
   invalidateAndReleaseEventStream(streamRef);
}

// track active handles so we can implement unregisterAll
std::list<Handle> s_activeHandles;


} // anonymous namespace

namespace detail {

// register a new file monitor
void registerMonitor(const core::FilePath& filePath, const Callbacks& callbacks)
{
   // allocate file path
   std::string path = filePath.absolutePath();
   CFStringRef filePathRef = ::CFStringCreateWithCString(
                                       kCFAllocatorDefault,
                                       filePath.absolutePath().c_str(),
                                       kCFStringEncodingUTF8);
   if (filePathRef == NULL)
   {
      callbacks.onRegistrationError(systemError(
                                       boost::system::errc::not_enough_memory,
                                       ERROR_LOCATION));
      return;
   }
   CFRefScope filePathRefScope(filePathRef);

   // allocate paths array
   CFArrayRef pathsArrayRef = ::CFArrayCreate(kCFAllocatorDefault,
                                              (const void **)&filePathRef,
                                              1,
                                              NULL);
   if (pathsArrayRef == NULL)
   {
      callbacks.onRegistrationError(systemError(
                                       boost::system::errc::not_enough_memory,
                                       ERROR_LOCATION));
      return;
   }
   CFRefScope pathsArrayRefScope(pathsArrayRef);


   // create and allocate FileEventContext (create auto-ptr in case we
   // return early, we'll call release later before returning)
   FileEventContext* pContext = new FileEventContext();
   std::auto_ptr<FileEventContext> autoPtrContext(pContext);
   FSEventStreamContext context;
   context.version = 0;
   context.info = (void*) pContext;
   context.retain = NULL;
   context.release = NULL;
   context.copyDescription = NULL;

   // create the stream and save a reference to it
   pContext->streamRef = ::FSEventStreamCreate(
                  kCFAllocatorDefault,
                  &fileEventCallback,
                  &context,
                  pathsArrayRef,
                  kFSEventStreamEventIdSinceNow,
                  1,
                  kFSEventStreamCreateFlagNoDefer |
                  kFSEventStreamCreateFlagWatchRoot);
   if (pContext->streamRef == NULL)
   {
      callbacks.onRegistrationError(systemError(
                                       boost::system::errc::no_stream_resources,
                                       ERROR_LOCATION));
      return;
   }

   // schedule with the run loop
   ::FSEventStreamScheduleWithRunLoop(pContext->streamRef,
                                      ::CFRunLoopGetCurrent(),
                                      kCFRunLoopDefaultMode);

   // start the event stream (check for errors and release if necessary
   if (!::FSEventStreamStart(pContext->streamRef))
   {
      invalidateAndReleaseEventStream(pContext->streamRef);

      callbacks.onRegistrationError(systemError(
                                       boost::system::errc::no_stream_resources,
                                       ERROR_LOCATION));
      return;

   }

   // scan the files
   Error error = scanFiles(FileInfo(filePath),
                           true,
                           &pContext->fileTree);
   if (error)
   {
       // stop, invalidate, release
       stopInvalidateAndReleaseEventStream(pContext->streamRef);

       // return error
       callbacks.onRegistrationError(error);
       return;
   }

   // now that we have finished the file listing we know we have a valid
   // file-monitor so set the onFilesChanged callback so that the
   // client can receive events
   pContext->onFilesChanged = callbacks.onFilesChanged;

   // we are going to pass the context pointer to the client (as the Handle)
   // so we release it here to relinquish ownership
   autoPtrContext.release();

   // track the handle
   s_activeHandles.push_back((Handle*)pContext);

   // notify the caller that we have successfully registered
   callbacks.onRegistered((Handle)pContext, pContext->fileTree);
}

// unregister a file monitor
void unregisterMonitor(Handle handle)
{
   // cast to context
   FileEventContext* pContext = (FileEventContext*)handle;

   // stop, invalidate, release
   stopInvalidateAndReleaseEventStream(pContext->streamRef);

   // untrack the handle
   s_activeHandles.remove(handle);

   // delete context
   delete pContext;
}

void unregisterAll()
{
   // make a copy of all active handles so we can unregister them
   // (unregistering mutates the list so that's why we need a copy)
   std::vector<Handle> activeHandles;
   std::copy(s_activeHandles.begin(),
             s_activeHandles.end(),
             std::back_inserter(activeHandles));

   // unregister all
   std::for_each(activeHandles.begin(), activeHandles.end(), unregisterMonitor);
}

void run(const boost::function<void()>& checkForInput)
{
   // ensure we have a run loop for this thread (not sure if this is
   // strictly necessary but it is not harmful)
   ::CFRunLoopGetCurrent();

   while (true)
   {
      // process the run loop for 1 second
      SInt32 reason = ::CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, false);

      // if we were stopped then break
      if (reason == kCFRunLoopRunStopped)
      {
         unregisterAll();
         break;
      }

      // check for input
      checkForInput();
   }
}

} // namespace detail
} // namespace file_monitor
} // namespace system
} // namespace core 

   


