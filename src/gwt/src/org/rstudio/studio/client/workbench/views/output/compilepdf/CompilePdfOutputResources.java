/*
 * CompilePdfOutputResources.java
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


import com.google.gwt.core.client.GWT;
import com.google.gwt.resources.client.ClientBundle;
import com.google.gwt.resources.client.CssResource;
import com.google.gwt.resources.client.ImageResource;

public interface CompilePdfOutputResources extends ClientBundle
{  
   public static interface Styles extends CssResource
   {
      String table();
      String headerRow();
      String selectedRow();
      String iconCell();
      String errorIcon();
      String warningIcon();
      String boxIcon();
      String lineCell();
      String messageCell();
   }

   @Source("images/error.png")
   ImageResource error();
  
   @Source("org/rstudio/core/client/theme/res/errorSmall.png")
   ImageResource warning();

   @Source("images/badbox.png")
   ImageResource badbox();

   @Source("CompilePdfOutput.css")
   Styles styles();
   
   
   public static CompilePdfOutputResources INSTANCE = 
      (CompilePdfOutputResources)GWT.create(CompilePdfOutputResources.class) ;
  
}