# axmol-migrate

Migrate exist projects of cocos2d-x or older axmol to compile with latest axmol engine

**NOTICE: please ensure your project in SCM before use use this tool**

## Whey needs

- axmol 1.x latest change remove `CC` prefix from sources files, so if exist projects not include `cocos2d.h` only, or include 
other header files of engine, you can use this tool to fast migrate your sources code includes.
- axmol 2.0 migrating `GLSL 100` to modern `ESSL 310`, this tool also support migrate exist `GLSL 100` to `ESSL 310`

## steps

1. clone this repo
2. just run the command `axmol` in this project root
3. do migrate:
    - migrate c++ code: `pwsh .\build_x64\axmol-migrate code --fuzzy --source-dir <path/to/your/project/>`
    - migrate shader file: `pwsh .\build_x64\axmol-migrate shader --source-dir <path/to/your/shaders/>`
