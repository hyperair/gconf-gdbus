/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef GCONF_GCONF_CXX_H
#define GCONF_GCONF_CXX_H

#include <string.h>
#include <vector>
#include <pair>

class G_Conf { /* This is a crude hack to avoid using a namespace */
public:

  class Exception {
  public:
    virtual const char* what() const = 0;

  };

  /* Failure communicating with the config server */
  class CommError : public Exception {
    virtual const char* what() const;
  }

  /* Type requested doesn't match type found */
  class WrongType : public Exception {
    virtual const char* what() const;
  }

  /* Key is malformed */
  class BadKey : public Exception {
    virtual const char* what() const;
  }
  
  class DirImpl;
  
  /* A dir represents a single configuration directory; it
     does some client-side caching for efficiency */
  class Dir {
  public:
    Dir(const string& dirname);
    ~Dir();

    /* keys are relative to the dir */

    void set(const string& key, int val);
    void set(const string& key, bool val);
    void set(const string& key, double val);
    void set(const string& key, const string& val);
    void set(const string& key, const vector<string>& val);
    void set(const string& key, const vector<int>& val);
    void set(const string& key, const vector<bool>& val);
    void set(const string& key, const vector<double>& val);
    
    void unset(const string& key);

    /* return false if the value wasn't retrieved due to nonexistence.
       throws an exception if not retrieved due to an error.
       These aren't const methods, since they actually change quite a bit
       of state; it would be misleading.
    */
    bool get(const string& key, int* val);
    bool get(const string& key, bool* val);
    bool get(const string& key, double* val);
    bool get(const string& key, string* val);
    bool get(const string& key, vector<string>* val);
    bool get(const string& key, vector<int>* val);
    bool get(const string& key, vector<bool>* val);
    bool get(const string& key, vector<double>* val);
    
    
    
  private:
    DirImpl* impl_;
  };
  

};


#endif



