.section .text
.global context_switch

#stack layout for a new process, starting at stack_ptr (for person 3): 
#0-28(%esp): eight 4 byte register slots for popal
#32(%esp): entry function address, used by ret
#36(%esp): exit routine address, used it entry returns

context_switch:
	#checking if the new process is null

	movl 8(%esp), %eax
	testl %eax, %eax
	je .return

	#checking if new->stack_ptr is valid
	cmpl $0, 8(%eax)
	je .return

	#saving registers
	pushal

	#saving old process stack ptr if old isnt null
	movl 36(%esp), %eax
	testl %eax, %eax
	je .load_new
	movl %esp, 8(%eax)

.load_new:
	#getting the new process ptr
	movl 40(%esp), %eax
	
	#loading new process stack ptr
	movl 8(%eax), %esp

	#restoring the registers & jumping to the new process
	popal
	ret

.return:
	ret

.section .note.GNU-stack,"",@progbits	
