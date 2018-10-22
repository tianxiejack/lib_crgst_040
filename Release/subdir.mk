################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../gst_capture.cpp 

OBJS += \
./gst_capture.o 

CPP_DEPS += \
./gst_capture.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: NVCC Compiler'
	/usr/local/cuda-8.0/bin/nvcc -U__CUDA_PREC_SQRT -U__CUDA_FTZ -U__CUDA_ARCH__ -UCUDA_DOUBLE_MATH_FUNCTIONS -U__CUDA_RUNTIME_H__ -U__DRIVER_TYPES_H__ -U__CUDANVVM__ -U__USE_FAST_MATH__ -U__CUDABE__ -U__CUDA_PREC_DIV -I/usr/lib/aarch64-linux-gnu/gstreamer-1.0/include -I/usr/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -I/usr/lib/aarch64-linux-gnu/include -I../cr_osa/inc -I/usr/include/GL -I../cr_osa/src -O3 -Xcompiler -fPIC -std=c++11 -ccbin aarch64-linux-gnu-g++ -gencode arch=compute_20,code=sm_20 -m64 -odir "." -M -o "$(@:%.o=%.d)" "$<"
	/usr/local/cuda-8.0/bin/nvcc -U__CUDA_PREC_SQRT -U__CUDA_FTZ -U__CUDA_ARCH__ -UCUDA_DOUBLE_MATH_FUNCTIONS -U__CUDA_RUNTIME_H__ -U__DRIVER_TYPES_H__ -U__CUDANVVM__ -U__USE_FAST_MATH__ -U__CUDABE__ -U__CUDA_PREC_DIV -I/usr/lib/aarch64-linux-gnu/gstreamer-1.0/include -I/usr/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -I/usr/lib/aarch64-linux-gnu/include -I../cr_osa/inc -I/usr/include/GL -I../cr_osa/src -O3 -Xcompiler -fPIC -std=c++11 --compile -m64 -ccbin aarch64-linux-gnu-g++  -x c++ -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


