# axmol-migrate

Migrate exist projects of cocos2d-x or older axmol to compile with latest axmol engine,

## Whey needs

- axmol latest change remove `CC` prefix from sources files, so if exist projects not include `cocos2d.h` only, or include 
other header files of engine, you can use this tool to fast migrate your sources code includes.
- axmol migrating `GLSL 100` to modern `ESSL 310`, this tool also support migrate exist `GLSL 100` to `ESSL 310`

## steps

1. clone this repo
2. double click the `build.ps1` of this project
3. do migrate:
    - migrate c++ code: `pwsh .\build_x64\axmol-migrate source --fuzzy --source-dir <path/to/your/project/>`
    - migrate shader file: `pwsh .\build_x64\axmol-migrate shader --source-dir <path/to/your/shaders/>`
