# RTOS_VCU_OSEK
Đây là mã nguồn dự án cá nhân của Nguyễn Tuấn Khoa. Dự án này dùng để phát triển một RTOS tuỳ chỉnh căn bản được viết theo tiêu chuẩn OSEK. Dự án sẽ có một kernel và có các chức năng như Eventmask, Alarm & Counter, Schedule Table, IOC, Resources Management.... Dự án đã có chức năng EventMask và Alarm, vẫn còn đang phát triển.
## Chi tiết thành phần dự án
- app : chứa task mà bộ lập lịch có thể gọi
- Config: Chứa các cài đặt tuỳ chỉnh cho Os
-  OS: chứa kernel và port của OS
-  SPL: thư viện Standard Peripheral library
-  Tools: chứa file .s, .o, .bin và .elf
