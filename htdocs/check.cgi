#!/usr/bin/env python3

import cgi
import cgitb

cgitb.enable()  # 用于调试

print("Content-Type: text/html\n")  # 输出 HTTP 头部
print("<html>")
print("<head>")
print("<title>Example CGI script</title>")
print("</head>")
print("<body bgcolor='red'>")
print("<h1>CGI Example</h1>")
print("<p>This is an example of CGI</p>")
print("<p>Parameters given to this script:</p>")
print("<ul>")

form = cgi.FieldStorage()
for param in form.keys():
    print(f"<li>{param}: {form.getvalue(param)}</li>")

print("</ul>")
print("</body>")
print("</html>")
