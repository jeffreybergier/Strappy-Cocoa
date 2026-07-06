# TODO

- Investigate Apple MediaLibrary's private `ML3ContainerItemToContainer`
  SQLite virtual table module. Strappy should keep ordinary MediaLibrary table
  queries working when this module is unavailable, and later decide whether a
  Cocoa/MediaLibrary bridge can safely register or replace the virtual table.
