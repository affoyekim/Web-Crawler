#ifndef _URL_H_
#define _URL_H_

#include <set>
#include <string>
using namespace std;

#define MAX_READ 131072
#define MAX_THREADS 50

// Describes URLs parsed by parse_URLs
struct url_t{
  string host;
  string file;
};

// Necessary for inserting URLs into a set
struct lturl
{
  bool operator()(const url_t& u1, const url_t& u2) const
  {
    bool ret = (u1.host == u2.host);
    if (ret)
      return u1.file < u2.file;
    else return (u1.host < u2.host);
  }
};

// Parses a single url from a string
// This assumes that the url starts with http:// and is well-formatted
// This can be used for the starting URL
// Returns -1 on an error
int parse_single_URL(string input, string& host, string& file);
	
// Parses a set of URLs out of a buffer of length size
// It is a somewhat picky about the format of those URLs, so in the general Internet it will not catch every URL.
// After running urls will contain these URLs 
// You can declare your own set of urls with: set<url_t,lturl> urls;
// Returns -1 on error (size is bigger than MAX_READ, null buf)
int parse_URLs(char *buf, int size, set<url_t,lturl> &urls);

#endif
