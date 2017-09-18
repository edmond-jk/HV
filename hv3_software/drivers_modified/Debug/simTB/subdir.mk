################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../simTB/hv_discovery.c \
../simTB/hv_test.c 

OBJS += \
./simTB/hv_discovery.o \
./simTB/hv_test.o 

C_DEPS += \
./simTB/hv_discovery.d \
./simTB/hv_test.d 


# Each subdirectory must supply rules for building sources it contributes
simTB/%.o: ../simTB/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


