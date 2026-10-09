/* SemCraft 2 - just enough JSON to read the Minecraft mod's flat messages ({"t":"hurt","d":12.5,"from":[1,2,3]}).
 * Keys are looked up at the top level only; strings without escapes other than \" and \\. */
#ifndef SEMCRAFT2_JSON_H
#define SEMCRAFT2_JSON_H

#include <string>
#include <stdlib.h>

namespace json {

// Position just after `"key":` at nesting depth 1, or npos.
inline size_t FindValue(const std::string &s, const char *strKey)
{
  const std::string strNeedle = std::string("\"") + strKey + "\"";
  int iDepth = 0;
  bool bInString = false;

  for (size_t i = 0; i < s.size(); i++) {
    const char c = s[i];

    if (bInString) {
      if (c == '\\') { i++; continue; }
      if (c == '"') bInString = false;
      continue;
    }

    if (c == '{' || c == '[') { iDepth++; continue; }
    if (c == '}' || c == ']') { iDepth--; continue; }

    if (c == '"') {
      if (iDepth == 1 && s.compare(i, strNeedle.size(), strNeedle) == 0) {
        size_t j = i + strNeedle.size();
        while (j < s.size() && (s[j] == ' ' || s[j] == ':')) j++;
        return j;
      }
      bInString = true;
    }
  }
  return std::string::npos;
}

inline bool GetString(const std::string &s, const char *strKey, std::string &strOut)
{
  size_t i = FindValue(s, strKey);
  if (i == std::string::npos || i >= s.size() || s[i] != '"') return false;

  strOut.clear();
  for (i++; i < s.size() && s[i] != '"'; i++) {
    if (s[i] == '\\' && i + 1 < s.size()) i++;
    strOut += s[i];
  }
  return true;
}

inline bool GetNumber(const std::string &s, const char *strKey, double &dOut)
{
  size_t i = FindValue(s, strKey);
  if (i == std::string::npos) return false;

  const char *pStart = s.c_str() + i;
  char *pEnd = NULL;
  dOut = strtod(pStart, &pEnd);
  return pEnd != pStart;
}

inline bool GetBool(const std::string &s, const char *strKey, bool &bOut)
{
  size_t i = FindValue(s, strKey);
  if (i == std::string::npos) return false;

  if (s.compare(i, 4, "true") == 0) { bOut = true; return true; }
  if (s.compare(i, 5, "false") == 0) { bOut = false; return true; }
  return false;
}

// Reads up to ctMax numbers of an array value; returns how many were read.
inline int GetNumbers(const std::string &s, const char *strKey, double *adOut, int ctMax)
{
  size_t i = FindValue(s, strKey);
  if (i == std::string::npos || i >= s.size() || s[i] != '[') return 0;

  const char *p = s.c_str() + i + 1;
  int ct = 0;

  while (ct < ctMax) {
    while (*p == ' ' || *p == ',') p++;
    if (*p == ']' || *p == 0) break;

    char *pEnd = NULL;
    adOut[ct] = strtod(p, &pEnd);
    if (pEnd == p) break;
    ct++;
    p = pEnd;
  }
  return ct;
}

// Escapes a string for a JSON string value.
inline std::string Escape(const char *str)
{
  std::string strOut;
  for (; *str != 0; str++) {
    const char c = *str;
    if (c == '"' || c == '\\') { strOut += '\\'; strOut += c; }
    else if ((unsigned char)c < 0x20) { strOut += ' '; }
    else { strOut += c; }
  }
  return strOut;
}

}; // namespace

#endif
