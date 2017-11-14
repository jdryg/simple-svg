# simple-svg
SVG parser, writer and helper functions

### Cloning
```
git clone --recursive https://github.com/jdryg/simple-svg.git
```

### Code

* Library: `src/svg/*`
	- `svg.h`: Struct, enums and function declarations
	- `svg.cpp`: Generic library functions for dealing with images, shape lists, point lists and paths
	- `svg_parser.cpp`: SVG parser
	- `svg_writer.cpp`: SVG writer
	- `svg_builder.cpp`: Helper functions for building images
* Demo: `src/main.cpp`

### License

	MIT License

	Copyright (c) 2017 Jim Drygiannakis

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