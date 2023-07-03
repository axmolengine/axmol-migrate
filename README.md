# axmol-migrate

Migrate exist projects of cocos2d-x or older axmol to compile with latest axmol engine,

## Whey needs

axmol latest change remove `CC` prefix from sources files, so if exists project not include `cocos2d.h` only, or include 
other header files of engine, you can use this tool to fast migreate your sources code includes.

## steps

1. clone this repo
2. double click the `buld.ps1` of this project
3. powershell .\build_x64\axmol-migrate --fuzzy --source-dir <path/to/your/project/>
