// @file mutexdebugger.h

/*    Copyright 2014 MongoDB Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include "mongo/client/undef_macros.h"

#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "boost/thread/mutex.hpp"

#include "mongo/client/redef_macros.h"

#include "mongo/util/assert_util.h"

namespace mongo {

    /** only used on _DEBUG builds.
        MutexDebugger checks that we always acquire locks for multiple mutexes in a consistent (acyclic) order.
        If we were inconsistent we could deadlock.
    */
    class MutexDebugger {
        typedef const char * mid; // mid = mutex ID
        typedef std::map<mid,int> Preceeding;
        std::map< mid, int > maxNest;
        boost::thread_specific_ptr< Preceeding > us;
        std::map< mid, std::set<mid> > followers;
        boost::mutex &x;
        unsigned magic;
        void aBreakPoint() { } // for debugging
    public:
        // set these to create an assert that
        //   b must never be locked before a
        //   so
        //     a.lock(); b.lock(); is fine
        //     b.lock(); alone is fine too
        //   only checked on _DEBUG builds.
        std::string a,b;

        /** outputs some diagnostic info on mutexes (on _DEBUG builds) */
        void programEnding();

        MutexDebugger();

        std::string currentlyLocked() const { 
            Preceeding *_preceeding = us.get();
            if( _preceeding == 0 )
                return "";
            Preceeding &preceeding = *_preceeding;
            std::stringstream q;
            for( Preceeding::const_iterator i = preceeding.begin(); i != preceeding.end(); i++ ) {
                if( i->second > 0 )
                    q << "  " << i->first << ' ' << i->second << '\n';
            }
            return q.str();
        }

        void entering(mid m) {
            if( this == 0 || m == 0 ) return;
            verify( magic == 0x12345678 );

            Preceeding *_preceeding = us.get();
            if( _preceeding == 0 )
                us.reset( _preceeding = new Preceeding() );
            Preceeding &preceeding = *_preceeding;

            if( a == m ) {
                aBreakPoint();
                if( preceeding[b.c_str()] ) {
                    std::cout << "****** MutexDebugger error! warning " << b << " was locked before " << a << std::endl;
                    verify(false);
                }
            }

            preceeding[m]++;
            if( preceeding[m] > 1 ) {
                // recursive re-locking.
                if( preceeding[m] > maxNest[m] )
                    maxNest[m] = preceeding[m];
                return;
            }

            bool failed = false;
            std::string err;
            {
                boost::mutex::scoped_lock lk(x);
                followers[m];
                for( Preceeding::iterator i = preceeding.begin(); i != preceeding.end(); i++ ) {
                    if( m != i->first && i->second > 0 ) {
                        followers[i->first].insert(m);
                        if( followers[m].count(i->first) != 0 ) {
                            failed = true;
                            std::stringstream ss;
                            mid bad = i->first;
                            ss << "mutex problem" <<
                               "\n  when locking " << m <<
                               "\n  " << bad << " was already locked and should not be."
                               "\n  set a and b above to debug.\n";
                            std::stringstream q;
                            for( Preceeding::iterator i = preceeding.begin(); i != preceeding.end(); i++ ) {
                                if( i->first != m && i->first != bad && i->second > 0 )
                                    q << "  " << i->first << '\n';
                            }
                            std::string also = q.str();
                            if( !also.empty() )
                                ss << "also locked before " << m << " in this thread (no particular order):\n" << also;
                            err = ss.str();
                            break;
                        }
                    }
                }
            }
            if( failed ) {
                std::cout << err << std::endl;
                verify( 0 );
            }
        }
        void leaving(mid m) {
            if( this == 0 || m == 0 ) return; // still in startup pre-main()
            Preceeding& preceeding = *us.get();
            preceeding[m]--;
            if( preceeding[m] < 0 ) {
                std::cout << "ERROR: lock count for " << m << " is " << preceeding[m] << std::endl;
                verify( preceeding[m] >= 0 );
            }
        }
    };
    extern MutexDebugger &mutexDebugger;

}
