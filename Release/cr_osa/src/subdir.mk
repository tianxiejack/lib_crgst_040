################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../cr_osa/src/osa.cpp \
../cr_osa/src/osa_buf.cpp \
../cr_osa/src/osa_eth_client.cpp \
../cr_osa/src/osa_eth_server.cpp \
../cr_osa/src/osa_event.cpp \
../cr_osa/src/osa_file.cpp \
../cr_osa/src/osa_i2c.cpp \
../cr_osa/src/osa_image_queue.cpp \
../cr_osa/src/osa_mbx.cpp \
../cr_osa/src/osa_msgq.cpp \
../cr_osa/src/osa_mutex.cpp \
../cr_osa/src/osa_pipe.cpp \
../cr_osa/src/osa_prf.cpp \
../cr_osa/src/osa_que.cpp \
../cr_osa/src/osa_rng.cpp \
../cr_osa/src/osa_sem.cpp \
../cr_osa/src/osa_thr.cpp \
../cr_osa/src/osa_tsk.cpp 

OBJS += \
./cr_osa/src/osa.o \
./cr_osa/src/osa_buf.o \
./cr_osa/src/osa_eth_client.o \
./cr_osa/src/osa_eth_server.o \
./cr_osa/src/osa_event.o \
./cr_osa/src/osa_file.o \
./cr_osa/src/osa_i2c.o \
./cr_osa/src/osa_image_queue.o \
./cr_osa/src/osa_mbx.o \
./cr_osa/src/osa_msgq.o \
./cr_osa/src/osa_mutex.o \
./cr_osa/src/osa_pipe.o \
./cr_osa/src/osa_prf.o \
./cr_osa/src/osa_que.o \
./cr_osa/src/osa_rng.o \
./cr_osa/src/osa_sem.o \
./cr_osa/src/osa_thr.o \
./cr_osa/src/osa_tsk.o 

CPP_DEPS += \
./cr_osa/src/osa.d \
./cr_osa/src/osa_buf.d \
./cr_osa/src/osa_eth_client.d \
./cr_osa/src/osa_eth_server.d \
./cr_osa/src/osa_event.d \
./cr_osa/src/osa_file.d \
./cr_osa/src/osa_i2c.d \
./cr_osa/src/osa_image_queue.d \
./cr_osa/src/osa_mbx.d \
./cr_osa/src/osa_msgq.d \
./cr_osa/src/osa_mutex.d \
./cr_osa/src/osa_pipe.d \
./cr_osa/src/osa_prf.d \
./cr_osa/src/osa_que.d \
./cr_osa/src/osa_rng.d \
./cr_osa/src/osa_sem.d \
./cr_osa/src/osa_thr.d \
./cr_osa/src/osa_tsk.d 


# Each subdirectory must supply rules for building sources it contributes
cr_osa/src/%.o: ../cr_osa/src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: NVCC Compiler'
	/usr/local/cuda-8.0/bin/nvcc -U__CUDA_PREC_SQRT -U__CUDA_FTZ -U__CUDA_ARCH__ -UCUDA_DOUBLE_MATH_FUNCTIONS -U__CUDA_RUNTIME_H__ -U__DRIVER_TYPES_H__ -U__CUDANVVM__ -U__USE_FAST_MATH__ -U__CUDABE__ -U__CUDA_PREC_DIV -I/usr/lib/aarch64-linux-gnu/gstreamer-1.0/include -I/usr/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -I/usr/lib/aarch64-linux-gnu/include -I../cr_osa/inc -I/usr/include/GL -I../cr_osa/src -O3 -Xcompiler -fPIC -std=c++11 -ccbin aarch64-linux-gnu-g++ -gencode arch=compute_20,code=sm_20 -m64 -odir "cr_osa/src" -M -o "$(@:%.o=%.d)" "$<"
	/usr/local/cuda-8.0/bin/nvcc -U__CUDA_PREC_SQRT -U__CUDA_FTZ -U__CUDA_ARCH__ -UCUDA_DOUBLE_MATH_FUNCTIONS -U__CUDA_RUNTIME_H__ -U__DRIVER_TYPES_H__ -U__CUDANVVM__ -U__USE_FAST_MATH__ -U__CUDABE__ -U__CUDA_PREC_DIV -I/usr/lib/aarch64-linux-gnu/gstreamer-1.0/include -I/usr/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -I/usr/lib/aarch64-linux-gnu/include -I../cr_osa/inc -I/usr/include/GL -I../cr_osa/src -O3 -Xcompiler -fPIC -std=c++11 --compile -m64 -ccbin aarch64-linux-gnu-g++  -x c++ -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


