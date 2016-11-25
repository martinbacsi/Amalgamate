/*
  ==============================================================================

  Copyright (c) 2012 by Vinnie Falco

  This file is provided under the terms of the MIT License:

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ==============================================================================
*/

#include <regex>
#include <unordered_map>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>

#include "juce_core_amalgam.h"

using namespace juce;

//==============================================================================

/* Handles the @remap directive for redirecting includes to amalgamated files.
*/
class RemapTable : public std::unordered_map <std::string, std::string>
{
public:
  RemapTable () : m_pattern (
    "[ \t]*/\\*[ \t]*@remap[ \t]+\"([^\"]+)\"[ \t]+\"([^\"]+)\"[ \t]*\\*/[ \t]*")
  {
  }

  // Returns true if the line was a @remap directive
  //
  bool processLine (std::string const& line)
  {
    bool wasRemap = false;

    std::match_results <std::string::const_iterator> result;

    if (std::regex_match (line, result, m_pattern))
    {
      const_iterator iter = find (result [1]);

      if (iter == end ())
      {
        this->operator[] (result[1]) = result[2];

        //std::cout << "@remap " << result[1] << " to " << result[2] << std::endl;
      }
      else
      {
        std::cout << "Warning: duplicate @remap directive" << std::endl;
      }
      
      wasRemap = true;
    }

    return wasRemap;
  }

private:
  std::regex m_pattern;
  std::unordered_map <std::string, std::string> m_value;
};

//==============================================================================

/* Handles #include lines.
*/
class IncludeProcessor
{
public:
  IncludeProcessor ()
    : m_includePattern ("[ \t]*#include[ \t]+(.*)[ t]*")
    , m_macroPattern ("([_a-zA-Z][_0-9a-zA-Z]*)")
    , m_anglePattern ("<([^>]+)>.*")
    , m_quotePattern ("\"([^\"]+)\".*")
  {

  }

  void processLine (std::string const& line)
  {
    std::match_results <std::string::const_iterator> r1;

    if (std::regex_match (line, r1, m_includePattern))
    {
      std::string s (r1[1]);

      std::match_results <std::string::const_iterator> r2;

      if (std::regex_match (s, r2, m_macroPattern))
      {
        // r2[1] holds the macro
      }
      else if (std::regex_match (s, r2, m_anglePattern))
      {
        // std::cout << "\"" << r2[1] << "\" ";
      }
      else if (std::regex_match (s, r2, m_quotePattern))
      {
        // std::cout << "\"" << r2[1] << "\" ";
      }
    }
  }

private:
  std::regex m_includePattern;
  std::regex m_macroPattern;
  std::regex m_anglePattern;
  std::regex m_quotePattern;
};

//==============================================================================
class Amalgamator
{
public:
  RemapTable m_remapTable;
  IncludeProcessor m_includeProcessor;

  explicit Amalgamator (String toolName)
    : m_name (toolName)
    , m_verbose (false)
    , m_checkSystemIncludes (false)
  {
    setWildcards ("*.cpp;*.c;*.h;*.mm;*.m");
  }

  const String name () const
  {
    return m_name;
  }

  void setCheckSystemIncludes (bool checkSystemIncludes)
  {
    m_checkSystemIncludes = checkSystemIncludes;
  }

  void setWildcards (String wildcards)
  {
    m_wildcards.clear ();
    m_wildcards.addTokens (wildcards, ";,", "'\"");
    m_wildcards.trim();
    m_wildcards.removeEmptyStrings();

  }

  void setTemplate (String fileName)
  {
    m_templateFile = File::getCurrentWorkingDirectory().getChildFile (fileName);
  }

  void setTarget (String fileName)
  {
    m_targetFile = File::getCurrentWorkingDirectory().getChildFile (fileName);
  }

  void setVerbose ()
  {
    m_verbose = true;
  }

  void addDirectoryToSearch (String directoryToSearch)
  {
    File dir (directoryToSearch);

    m_directoriesToSearch.add (dir.getFullPathName ());
  }

  void addPreventReinclude (String identifier)
  {
    m_preventReincludes.add (identifier);
  }

  void addForceReinclude (String identifier)
  {
    m_forceReincludes.add (identifier);
  }

  void addDefine (String name, String value)
  {
    m_macrosDefined.set (name, value);
  }

  bool process ()
  {
    bool error = false;

    // Make sure the template file exists

    if (! m_templateFile.existsAsFile())
    {
      std::cout << name () << " The template file doesn't exist!\n\n";
      error = true;
    }

    if (!error)
    {
      // Prepare to write output to a temporary file.

      std::cout << "  Building: " << m_targetFile.getFullPathName() << "...\n";

      TemporaryFile temp (m_targetFile);
      ScopedPointer <FileOutputStream> out (temp.getFile().createOutputStream (1024 * 128));

      if (out == 0)
      {
        std::cout << "  \n!! ERROR - couldn't write to the target file: "
          << temp.getFile().getFullPathName() << "\n\n";
        return false;
      }

      out->setNewLineString ("\n");

      if (! parseFile (m_targetFile.getParentDirectory(),
        m_targetFile,
        *out,
        m_templateFile,
        m_alreadyIncludedFiles,
        m_includesToIgnore,
        m_wildcards,
        0, false))
      {
        return false;
      }

      out = 0;

      if (calculateFileHashCode (m_targetFile) == calculateFileHashCode (temp.getFile()))
      {
        std::cout << "   -- No need to write - new file is identical\n";
        return true;
      }

      if (! temp.overwriteTargetFileWithTemporary())
      {
        std::cout << "  \n!! ERROR - couldn't write to the target file: "
          << m_targetFile.getFullPathName() << "\n\n";
        return false;
      }
    }

    return error;
  }

public:
  static int64 calculateStreamHashCode (InputStream& in)
  {
    int64 t = 0;

    const int bufferSize = 4096;
    HeapBlock <uint8> buffer;
    buffer.malloc (bufferSize);

    for (;;)
    {
      const int num = in.read (buffer, bufferSize);

      if (num <= 0)
        break;

      for (int i = 0; i < num; ++i)
        t = t * 65599 + buffer[i];
    }

    return t;
  }

  static int64 calculateFileHashCode (const File& file)
  {
    ScopedPointer <FileInputStream> stream (file.createInputStream());
    return stream != 0 ? calculateStreamHashCode (*stream) : 0;
  }

private:
  bool matchesWildcard (String const& filename)
  {
    for (int i = m_wildcards.size(); --i >= 0;)
      if (filename.matchesWildcard (m_wildcards[i], true))
        return true;

    return false;
  }

  static bool canFileBeReincluded (File const& f)
  {
    String content (f.loadFileAsString());

    for (;;)
    {
      content = content.trimStart();

      if (content.startsWith ("//"))
        content = content.fromFirstOccurrenceOf ("\n", false, false);
      else if (content.startsWith ("/*"))
        content = content.fromFirstOccurrenceOf ("*/", false, false);
      else
        break;
    }

    StringArray lines;
    lines.addLines (content);
    lines.trim();
    lines.removeEmptyStrings();

    const String l1 (lines[0].removeCharacters (" \t").trim());
    const String l2 (lines[1].removeCharacters (" \t").trim());

    bool result;
    if (l1.replace ("#ifndef", "#define") == l2)
      result = false;
    else
      result = true;

    return result;
  }

  File findInclude (File const& siblingFile, String filename)
  {
    File result;

    if (siblingFile.getSiblingFile (filename).existsAsFile ())
    {
      result = siblingFile.getSiblingFile (filename);
    }
    else
    {
      for (int i = 0; i < m_directoriesToSearch.size (); ++i)
      {
        if (File (m_directoriesToSearch[i]).getChildFile (filename).existsAsFile ())
        {
          result = File (m_directoriesToSearch[i]).getChildFile (filename);
          break;
        }
      }
    }

    return result;
  }

  void findAllFilesIncludedIn (const File& hppTemplate, StringArray& alreadyIncludedFiles)
  {
    StringArray lines;
    lines.addLines (hppTemplate.loadFileAsString());

    for (int i = 0; i < lines.size(); ++i)
    {
      String line (lines[i]);

      if (line.removeCharacters (" \t").startsWithIgnoreCase ("#include\""))
      {
        const String filename (line.fromFirstOccurrenceOf ("\"", false, false)
          .upToLastOccurrenceOf ("\"", false, false));
        const File targetFile (findInclude (hppTemplate, filename));

        if (! alreadyIncludedFiles.contains (targetFile.getFullPathName()))
        {
          alreadyIncludedFiles.add (targetFile.getFullPathName());

          findAllFilesIncludedIn (targetFile, alreadyIncludedFiles);
        }
      }
    }
  }
  struct ParsedInclude
  {
    ParsedInclude ()
      : isIncludeLine (false)
      , preventReinclude (false)
      , forceReinclude (false)
      , endOfInclude (0)
    {
    }

    bool isIncludeLine;
    bool preventReinclude;
    bool forceReinclude;
    int endOfInclude;
    String lineUpToEndOfInclude;
    String lineAfterInclude;
    String filename;
  };

  ParsedInclude parseInclude (String const& line, String const& trimmed)
  {
    ParsedInclude parsed;

    if (trimmed.startsWithChar ('#'))
    {
      const String removed = trimmed.removeCharacters (" \t");

      if (removed.startsWithIgnoreCase ("#include\""))
      {
        parsed.endOfInclude = line.indexOfChar (line.indexOfChar ('\"') + 1, '\"') + 1;
        parsed.filename = line.fromFirstOccurrenceOf ("\"", false, false)
                              .upToLastOccurrenceOf ("\"", false, false);

        parsed.isIncludeLine = true;
      }
      else if (removed.startsWithIgnoreCase ("#include<") && m_checkSystemIncludes)
      {
        parsed.endOfInclude = line.indexOfChar (line.indexOfChar ('<') + 1, '>') + 1;
        parsed.filename = line.fromFirstOccurrenceOf ("<", false, false)
                              .upToLastOccurrenceOf (">", false, false);
        parsed.isIncludeLine = true;
      }
      else if (removed.startsWithIgnoreCase ("#include"))
      {
        String name;

#if 1 
        if (line.contains ("/*"))
          name = line.fromFirstOccurrenceOf ("#include", false, false)
                     .upToFirstOccurrenceOf ("/*", false, false).trim ();
        else
          name = line.fromFirstOccurrenceOf ("#include", false, false).trim ();

        parsed.endOfInclude = line.upToFirstOccurrenceOf (name, true, false).length ();
#else
        name = line.fromFirstOccurrenceOf ("#include", false, false).trim ();
#endif

        String value = m_macrosDefined [name];

        if (m_verbose)
          std::cout << "name = " << name << "\n";

        if (value.startsWithIgnoreCase ("\""))
        {
          parsed.endOfInclude = line.trimEnd().length () + 1;
          parsed.filename = value.fromFirstOccurrenceOf ("\"", false, false)
                                .upToLastOccurrenceOf ("\"", false, false);

          parsed.isIncludeLine = true;
        }
        else if (value.startsWithIgnoreCase ("<") && m_checkSystemIncludes)
        {
          parsed.endOfInclude = line.trimEnd().length () + 1;
          parsed.filename = value.fromFirstOccurrenceOf ("<", false, false)
                                .upToLastOccurrenceOf (">", false, false);
          parsed.isIncludeLine = true;
        }

        parsed.preventReinclude = parsed.isIncludeLine && m_preventReincludes.contains (name);
        parsed.forceReinclude = parsed.isIncludeLine && m_forceReincludes.contains (name);
      }
    }

    return parsed;
  }

  bool parseFile (const File& rootFolder,
    const File& newTargetFile,
    OutputStream& dest,
    const File& file,
    StringArray& alreadyIncludedFiles,
    const StringArray& includesToIgnore,
    const StringArray& wildcards,
    int level,
    bool stripCommentBlocks)
  {
    // Make sure the file exists

    if (! file.exists())
    {
      std::cout << "  !! ERROR - file doesn't exist!";
      return false;
    }

    // Load the entire file as an array of individual lines.

    StringArray lines;
    lines.addLines (file.loadFileAsString());

    // Make sure there is something in the file.

    if (lines.size() == 0)
    {
      std::cout << "  !! ERROR - input file was empty: " << file.getFullPathName();
      return false;
    }

    // Produce some descriptive progress.

    if (m_verbose)
    {
      if (level == 0)
        std::cout << "  Processing \"" << file.getFileName () << "\"\n";
      else
        std::cout << "  Inlining " << String::repeatedString (" ", level - 1)
                  << "\"" << file.getFileName () << "\"\n";
    }

    bool lastLineWasBlank = true;

    for (int i = 0; i < lines.size(); ++i)
    {
      String line (lines[i]);
      String trimmed (line.trimStart());

      if ((level != 0) && trimmed.startsWith ("//================================================================"))
        line = String::empty;

      std::string s (line.toUTF8 ());

      if (m_remapTable.processLine (s))
      {
        line = String::empty;
      }
      else
      {
        ParsedInclude parsedInclude = parseInclude (line, trimmed);

        if (parsedInclude.isIncludeLine)
        {
          const File targetFile = findInclude (file, parsedInclude.filename);

          if (targetFile.exists())
          {
            if (matchesWildcard (parsedInclude.filename.replaceCharacter ('\\', '/'))
              && ! includesToIgnore.contains (targetFile.getFileName()))
            {
              String fileNamePart = targetFile.getFileName ();
              RemapTable::iterator remapTo = m_remapTable.find (std::string (fileNamePart.toUTF8 ()));
              if (level != 0 && remapTo != m_remapTable.end ())
              {
                line = "#include \"";
                line << String(remapTo->second.c_str ()) << "\"" << newLine;

                findAllFilesIncludedIn (targetFile, alreadyIncludedFiles);
              }
              else if (line.containsIgnoreCase ("FORCE_AMALGAMATOR_INCLUDE")
                || ! alreadyIncludedFiles.contains (targetFile.getFullPathName()))
              {
                if (parsedInclude.preventReinclude)
                {
                  alreadyIncludedFiles.add (targetFile.getFullPathName());
                }
                else if (parsedInclude.forceReinclude)
                {
                }
                else if (! canFileBeReincluded (targetFile))
                {
                  alreadyIncludedFiles.add (targetFile.getFullPathName());
                }

                dest << newLine << "/*** Start of inlined file: " << targetFile.getFileName() << " ***/" << newLine;

                if (! parseFile (rootFolder, newTargetFile,
                  dest, targetFile, alreadyIncludedFiles, includesToIgnore,
                  wildcards, level + 1, stripCommentBlocks))
                {
                  return false;
                }

                dest << "/*** End of inlined file: " << targetFile.getFileName() << " ***/" << newLine << newLine;

                line = parsedInclude.lineAfterInclude;
              }
              else
              {
                line = String::empty;
              }
            }
            else
            {
              line = parsedInclude.lineUpToEndOfInclude.upToFirstOccurrenceOf ("\"", true, false)
                + targetFile.getRelativePathFrom (newTargetFile.getParentDirectory())
                .replaceCharacter ('\\', '/')
                + "\""
                + parsedInclude.lineAfterInclude;
            }
          }
        }
      }

      if ((stripCommentBlocks || i == 0) && trimmed.startsWith ("/*") && (i > 10 || level != 0))
      {
        int originalI = i;
        String originalLine = line;

        for (;;)
        {
          int end = line.indexOf ("*/");

          if (end >= 0)
          {
            line = line.substring (end + 2);

            // If our comment appeared just before an assertion, leave it in, as it
            // might be useful..
            if (lines [i + 1].contains ("assert")
              || lines [i + 2].contains ("assert"))
            {
              i = originalI;
              line = originalLine;
            }

            break;
          }

          line = lines [++i];

          if (i >= lines.size())
            break;
        }

        line = line.trimEnd();
        if (line.isEmpty())
          continue;
      }

      line = line.trimEnd();

      {
        // Turn initial spaces into tabs..
        int numIntialSpaces = 0;
        int len = line.length();
        while (numIntialSpaces < len && line [numIntialSpaces] == ' ')
          ++numIntialSpaces;

        if (numIntialSpaces > 0)
        {
          int tabSize = 4;
          int numTabs = numIntialSpaces / tabSize;
          line = String::repeatedString ("\t", numTabs) + line.substring (numTabs * tabSize);
        }

  #if 0
        if (! line.containsChar ('"'))
        {
          // turn large areas of spaces into tabs - this will mess up alignment a bit, but
          // it's only the amalgamated file, so doesn't matter...
          line = line.replace ("        ", "\t", false);
          line = line.replace ("    ", "\t", false);
        }
  #endif
      }

      if (line.isNotEmpty() || ! lastLineWasBlank)
        dest << line << newLine;

      lastLineWasBlank = line.isEmpty();
    }

    return true;
  }

private:
  const String m_name;
  bool m_verbose;
  bool m_checkSystemIncludes;
  StringPairArray m_macrosDefined;
  StringArray m_wildcards;
  StringArray m_forceReincludes;
  StringArray m_preventReincludes;
  StringArray m_directoriesToSearch;
  StringArray m_alreadyIncludedFiles;
  StringArray m_includesToIgnore;

  File m_templateFile;
  File m_targetFile;
};

/******************************************************************************/

//==============================================================================








void RefreshDirectory(LPTSTR);
void RefreshTree(LPTSTR);
void WatchDirectory(LPTSTR);









int main(int argC, char** argV)
{
	//char* dir = "D:\\Amalgamate\\Builds\\VisualStudio2010\\";
	//char pwd[100];
	//GetCurrentDirectory(100, pwd);

	String homoDir = argV[1];
	String inFile = homoDir + String("CG.cpp");
	String outFile = homoDir + String("CG_OUT.cpp");


	Amalgamator amalgamator(inFile);
	amalgamator.setTemplate(inFile);
	amalgamator.setTarget(outFile);
	while (1)
	{
		WatchDirectory(argV[1]);
		
		amalgamator.process();
	}
}


void WatchDirectory(LPTSTR lpDir)
{
	DWORD dwWaitStatus;
	HANDLE dwChangeHandles[2];
	TCHAR lpDrive[4];
	TCHAR lpFile[_MAX_FNAME];
	TCHAR lpExt[_MAX_EXT];

	_tsplitpath_s(lpDir, lpDrive, 4, NULL, 0, lpFile, _MAX_FNAME, lpExt, _MAX_EXT);

	lpDrive[2] = (TCHAR)'\\';
	lpDrive[3] = (TCHAR)'\0';

	// Watch the directory for file creation and deletion. 

	dwChangeHandles[0] = FindFirstChangeNotification(
		lpDir,                         // directory to watch 
		FALSE,                         // do not watch subtree 
		FILE_NOTIFY_CHANGE_FILE_NAME); // watch file name changes 




	if (dwChangeHandles[0] == INVALID_HANDLE_VALUE)
	{
		printf("\n ERROR: FindFirstChangeNotification function failed.\n");
		ExitProcess(GetLastError());
	}

	// Watch the subtree for directory creation and deletion. 

	dwChangeHandles[1] = FindFirstChangeNotification(
		lpDir,                       // directory to watch 
		TRUE,                          // watch the subtree 
		FILE_NOTIFY_CHANGE_LAST_WRITE);  // watch dir name changes 

	if (dwChangeHandles[1] == INVALID_HANDLE_VALUE)
	{
		printf("\n ERROR: FindFirstChangeNotification function failed.\n");
		ExitProcess(GetLastError());
	}


	// Make a final validation check on our handles.

	if ((dwChangeHandles[0] == NULL) || (dwChangeHandles[1] == NULL))
	{
		printf("\n ERROR: Unexpected NULL from FindFirstChangeNotification.\n");
		ExitProcess(GetLastError());
	}

	// Change notification is set. Now wait on both notification 
	// handles and refresh accordingly. 

	while (TRUE)
	{
		// Wait for notification.

		printf("\nWaiting for notification...\n");

		dwWaitStatus = WaitForMultipleObjects(2, dwChangeHandles,
			FALSE, INFINITE);

		switch (dwWaitStatus)
		{
		case WAIT_OBJECT_0:

			// A file was created, renamed, or deleted in the directory.
			// Refresh this directory and restart the notification.

			RefreshDirectory(lpDir);
			if (FindNextChangeNotification(dwChangeHandles[0]) == FALSE)
			{
				printf("\n ERROR: FindNextChangeNotification function failed.\n");
				ExitProcess(GetLastError());
			}
			break;

		case WAIT_OBJECT_0 + 1:

			// A directory was created, renamed, or deleted.
			// Refresh the tree and restart the notification.
			return;
			RefreshTree(lpDir);
			if (FindNextChangeNotification(dwChangeHandles[1]) == FALSE)
			{
				printf("\n ERROR: FindNextChangeNotification function failed.\n");
				ExitProcess(GetLastError());
			}
			break;

		case WAIT_TIMEOUT:
			return;
			// A timeout occurred, this would happen if some value other 
			// than INFINITE is used in the Wait call and no changes occur.
			// In a single-threaded environment you might not want an
			// INFINITE wait.

			printf("\nNo changes in the timeout period.\n");
			break;

		default:
			printf("\n ERROR: Unhandled dwWaitStatus.\n");
			ExitProcess(GetLastError());
			break;
		}
	}
}

void RefreshDirectory(LPTSTR lpDir)
{
	// This is where you might place code to refresh your
	// directory listing, but not the subtree because it
	// would not be necessary.

	_tprintf(TEXT("Directory (%s) changed.\n"), lpDir);
}

void RefreshTree(LPTSTR lpDrive)
{
	// This is where you might place code to refresh your
	// directory listing, including the subtree.

	_tprintf(TEXT("Directory tree (%s) changed.\n"), lpDrive);
}
