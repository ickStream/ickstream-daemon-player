/*$*********************************************************************\

Name            : -

Source File     : config.h

Description     : Definitions for config.c 

Comments        : -

Date            : 20.02.2013 

Updates         : -

Author          : //MAF 

Remarks         : -


*************************************************************************
 This program is free software; you can redistribute it and/or modify   
 it under the terms of the GNU General Public License as published by   
 the Free Software Foundation; either version 2 of the License, or (at  
 your option) any later version.                                        
                                                                         
 This program is distributed in the hope that it will be useful, but    
 WITHOUT ANY WARRANTY; without even the implied warranty of             
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      
 General Public License for more details.                               
                                                                         
 You should have received a copy of the GNU General Public License      
 along with this program; if not, write to the Free Software            
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA              
 02111-1307, USA.  Or, point your browser to                            
 http://www.gnu.org/copyleft/gpl.html 
\*************************************************************************/

#ifndef __CONFIG_H
#define __CONFIG_H

/*========================================================================*\
   Prototypes
\*========================================================================*/

/*------------------------------------------------------------------------*\
   Parsing of config file
\*------------------------------------------------------------------------*/
int readconfig( const char *fname );

/*------------------------------------------------------------------------*\
   Argument handling
\*------------------------------------------------------------------------*/
int  addarg( const char *name, const char *sname, void *buffer, const char *valname, const char *help );
void usage( const char *prgname, int iserr );
int  getargs( int argc, char *argv[] );


#endif  /* __CONFIG_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

