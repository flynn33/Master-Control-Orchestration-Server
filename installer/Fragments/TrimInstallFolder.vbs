' Strip a trailing backslash from INSTALLFOLDER_CLEAN before deferred CAs
' compose their command lines. See CustomActions.wxs for the full rationale
' (CommandLineToArgvW quoting hazard).
Dim v
v = Session.Property("INSTALLFOLDER_CLEAN")
If Len(v) > 0 Then
  If Right(v, 1) = "\" Then v = Left(v, Len(v) - 1)
  Session.Property("INSTALLFOLDER_CLEAN") = v
End If
