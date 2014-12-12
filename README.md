## multipipelineview: Multi Pipe Line View

**Display the most recent non-empty line from each input**  

### Usage:

    nbudstee [inputs ...]
    nbudstee [options]

Where inputs ... are one or more files/FIFOs or stream-mode Unix domain socket files.

### Options:
* -h, --help  
  Show help text
* -V, --version  
  Show version information  

### Use Case:
Watching more than one line-based stream output on a terminal.  
This is particularly useful for progress/status outputs,  
e.g. pv, rsync --progress, wget --progress, etc.

### Notes:
* Each line is preceded by the given file name.  
* Inputs which have closed are marked with an 'X'.  
* Lines are truncated to the terminal width.  
* STDOUT must be a terminal.  

### URLs:
* This project is hosted at https://github.com/JGRennison/multipipelineview

### License:
GPLv2
