################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../hv_cache.c \
../hv_cdev.c \
../hv_cmd.c \
../hv_cmd_stress.c \
../hv_cmd_test.c \
../hv_mmio.c \
../hv_params.c \
../hv_profile.c \
../hv_queue.c \
../hv_smbus.c \
../hv_timer.c \
../hvdimm.c 

OBJS += \
./hv_cache.o \
./hv_cdev.o \
./hv_cmd.o \
./hv_cmd_stress.o \
./hv_cmd_test.o \
./hv_mmio.o \
./hv_params.o \
./hv_profile.o \
./hv_queue.o \
./hv_smbus.o \
./hv_timer.o \
./hvdimm.o 

C_DEPS += \
./hv_cache.d \
./hv_cdev.d \
./hv_cmd.d \
./hv_cmd_stress.d \
./hv_cmd_test.d \
./hv_mmio.d \
./hv_params.d \
./hv_profile.d \
./hv_queue.d \
./hv_smbus.d \
./hv_timer.d \
./hvdimm.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


