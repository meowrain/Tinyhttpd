#!/usr/bin/env python3

import cgi
import cgitb

cgitb.enable()  # 启用详细错误报告

def main():
    form = cgi.FieldStorage()

    # 默认颜色
    color = "blue"
    if "color" in form:
        color = form.getvalue("color")

    # 打印 HTTP 头部
    print("Content-Type: text/html\n")
    
    # 打印 HTML 内容
    print(f"""<!DOCTYPE html>
<html>
<head>
    <title>{color.upper()}</title>
</head>
<body bgcolor="{color}">
    <h1>This is {color}</h1>
</body>
</html>
""")

if __name__ == "__main__":
    main()
