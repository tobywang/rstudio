/*
 * CompilePdfOutputPresenter.java
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
package org.rstudio.studio.client.workbench.views.output.compilepdf;

import com.google.gwt.core.client.JsArray;
import com.google.gwt.event.dom.client.ClickEvent;
import com.google.gwt.event.dom.client.ClickHandler;
import com.google.gwt.event.dom.client.HasClickHandlers;
import com.google.gwt.user.client.Command;
import com.google.inject.Inject;
import org.rstudio.core.client.CodeNavigationTarget;
import org.rstudio.core.client.CommandUtil;
import org.rstudio.core.client.events.HasEnsureHiddenHandlers;
import org.rstudio.core.client.events.HasSelectionCommitHandlers;
import org.rstudio.core.client.events.SelectionCommitEvent;
import org.rstudio.core.client.events.SelectionCommitHandler;
import org.rstudio.core.client.files.FileSystemItem;
import org.rstudio.core.client.widget.MessageDialog;
import org.rstudio.core.client.widget.Operation;
import org.rstudio.core.client.widget.ProgressIndicator;
import org.rstudio.studio.client.common.GlobalDisplay;
import org.rstudio.studio.client.common.GlobalProgressDelayer;
import org.rstudio.studio.client.common.filetypes.FileTypeRegistry;
import org.rstudio.studio.client.server.ServerError;
import org.rstudio.studio.client.server.ServerRequestCallback;
import org.rstudio.studio.client.server.VoidServerRequestCallback;
import org.rstudio.studio.client.workbench.WorkbenchView;
import org.rstudio.studio.client.workbench.views.BasePresenter;
import org.rstudio.studio.client.workbench.views.output.compilepdf.events.CompilePdfErrorsEvent;
import org.rstudio.studio.client.workbench.views.output.compilepdf.events.CompilePdfEvent;
import org.rstudio.studio.client.workbench.views.output.compilepdf.events.CompilePdfOutputEvent;
import org.rstudio.studio.client.workbench.views.output.compilepdf.events.CompilePdfStatusEvent;
import org.rstudio.studio.client.workbench.views.output.compilepdf.model.CompilePdfError;
import org.rstudio.studio.client.workbench.views.output.compilepdf.model.CompilePdfServerOperations;
import org.rstudio.studio.client.workbench.views.output.compilepdf.model.CompilePdfState;


public class CompilePdfOutputPresenter extends BasePresenter
   implements CompilePdfEvent.Handler,
              CompilePdfOutputEvent.Handler, 
              CompilePdfErrorsEvent.Handler,
              CompilePdfStatusEvent.Handler
{
   public interface Display extends WorkbenchView, HasEnsureHiddenHandlers
   {
      void compileStarted(String text);
      void showOutput(String output);
      void showErrors(JsArray<CompilePdfError> errors);
      void clearAll();
      void compileCompleted();
      HasClickHandlers stopButton();
      HasSelectionCommitHandlers<CodeNavigationTarget> errorList();
   }

   @Inject
   public CompilePdfOutputPresenter(Display view,
                                    GlobalDisplay globalDisplay,
                                    CompilePdfServerOperations server,
                                    FileTypeRegistry fileTypeRegistry)
   {
      super(view);
      view_ = view;
      globalDisplay_ = globalDisplay;
      server_ = server;
      fileTypeRegistry_ = fileTypeRegistry;
      
      view_.stopButton().addClickHandler(new ClickHandler() {
         @Override
         public void onClick(ClickEvent event)
         {
            terminateCompilePdf(null);
         }
         
      });
      
      view_.errorList().addSelectionCommitHandler(
                              new SelectionCommitHandler<CodeNavigationTarget>() {

         @Override
         public void onSelectionCommit(
                              SelectionCommitEvent<CodeNavigationTarget> event)
         {
            CodeNavigationTarget target = event.getSelectedItem();
            FileSystemItem fsi = FileSystemItem.createFile(target.getFile());
            fileTypeRegistry_.editFile(fsi, target.getPosition());
         }
      });
   }
   
   public void initialize(CompilePdfState compilePdfState)
   {
      // TODO: this should really just ensure that the tab is available
      // rather than brinning it to the front
      view_.bringToFront();
      
      view_.clearAll();
      
      view_.compileStarted(compilePdfState.getTargetFile());
      
      view_.showOutput(compilePdfState.getOutput());
      
      if (compilePdfState.getErrors().length() > 0)
         view_.showErrors(compilePdfState.getErrors());    
      
      if (!compilePdfState.isRunning())
         view_.compileCompleted();
   }
   
   public void confirmClose(Command onConfirmed)
   {  
      // wrap the onConfirmed in another command which notifies the server
      // that we've closed the tab
      final Command confirmedCommand = CommandUtil.join(onConfirmed, 
                                                        new Command() {
         @Override
         public void execute()
         {
            server_.compilePdfClosed(new VoidServerRequestCallback());
         }
      });
      
      server_.isCompilePdfRunning(
                  new RequestCallback<Boolean>("Closing Compile PDF...") {
         @Override
         public void onSuccess(Boolean isRunning)
         {  
            if (isRunning)
            {
               confirmTerminateRunningCompile("close the Compile PDF tab", 
                                              confirmedCommand);
            }
            else
            {
               confirmedCommand.execute();
            }
         }
      });
      
   }

   @Override
   public void onCompilePdf(CompilePdfEvent event)
   {
      view_.bringToFront();
      
      compilePdf(event.getTargetFile(), event.getCompletedAction());
   }
   
   @Override
   public void onCompilePdfOutput(CompilePdfOutputEvent event)
   {
      view_.showOutput(event.getOutput());
   }
   
   @Override
   public void onCompilePdfErrors(CompilePdfErrorsEvent event)
   {
      view_.showErrors(event.getErrors());
   }
   
   @Override
   public void onCompilePdfStatus(CompilePdfStatusEvent event)
   {
      if (event.getStatus() == CompilePdfStatusEvent.STARTED)
         view_.compileStarted(event.getText());
      else if (event.getStatus() == CompilePdfStatusEvent.COMPLETED)
         view_.compileCompleted();
   }
   
   private void compilePdf(final FileSystemItem targetFile,
                           final String completedAction)
   {
      server_.compilePdf(
            targetFile, 
            completedAction, 
            new RequestCallback<Boolean>("Compiling PDF...") 
            {
               @Override
               protected void onSuccess(Boolean started)
               {
                  if (!started)
                  {
                     confirmTerminateRunningCompile(
                           "start a new compilation",
                           new Command() {
   
                              @Override
                              public void execute()
                              {
                                 compilePdf(targetFile, completedAction);
                              }     
                           });
                  }
               }
         });
   }
   
   private void confirmTerminateRunningCompile(String operation,
                                               final Command onTerminated)
   {
      globalDisplay_.showYesNoMessage(
         MessageDialog.WARNING,
         "Stop Running Compile", 
         "There is a PDF compilation currently running. If you " +
         operation + " it will be terminated. Are you " +
         "sure you want to stop the running PDF compilation?", 
         new Operation() {
            @Override
            public void execute()
            {
               terminateCompilePdf(onTerminated);
            }},
            false);
   }
   
  
   private void terminateCompilePdf(final Command onTerminated)
   {
      server_.terminateCompilePdf(new RequestCallback<Boolean>(
                                    "Terminating PDF compilation...") {
         @Override
         protected void onSuccess(Boolean wasTerminated)
         {
            if (wasTerminated)
            {
               if (onTerminated != null)
                  onTerminated.execute(); 
            }
            else
            {
               globalDisplay_.showErrorMessage(
                    "Compile PDF",
                    "Unable to terminate PDF compilation. Please try again.");
            }
         }
      });
   }
   
   
   private abstract class RequestCallback<T> extends ServerRequestCallback<T>
   {
      public RequestCallback(String progressMessage)
      {
         indicator_ = new GlobalProgressDelayer(
            globalDisplay_,  500, progressMessage).getIndicator();
      }
      
      @Override
      public void onResponseReceived(T response)
      {
         indicator_.onCompleted();
         onSuccess(response);
      }
      
      protected abstract void onSuccess(T response);

      @Override
      public void onError(ServerError error)
      {
         indicator_.onError(error.getUserMessage());
      }
      
      private ProgressIndicator indicator_;   
   };
   
   private final Display view_;
   private final GlobalDisplay globalDisplay_;
   private final CompilePdfServerOperations server_;
   private final FileTypeRegistry fileTypeRegistry_;
}
