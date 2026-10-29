
bin/gr2_cfw.elf:     file format elf32-bigarm


Disassembly of section .text:

a0a00000 <_start>:
a0a00000:	b5f0      	push	{r4, r5, r6, r7, lr}
a0a00002:	4805      	ldr	r0, [pc, #20]	@ (a0a00018 <_start+0x18>)
a0a00004:	4905      	ldr	r1, [pc, #20]	@ (a0a0001c <_start+0x1c>)
a0a00006:	2200      	movs	r2, #0
a0a00008:	4288      	cmp	r0, r1
a0a0000a:	da02      	bge.n	a0a00012 <_start+0x12>
a0a0000c:	6002      	str	r2, [r0, #0]
a0a0000e:	3004      	adds	r0, #4
a0a00010:	e7fa      	b.n	a0a00008 <_start+0x8>
a0a00012:	f000 f805 	bl	a0a00020 <cfw_main>
a0a00016:	bdf0      	pop	{r4, r5, r6, r7, pc}
a0a00018:	a0a016fc 	.word	0xa0a016fc
a0a0001c:	a0a01714 	.word	0xa0a01714

a0a00020 <cfw_main>:
a0a00020:	b508      	push	{r3, lr}
a0a00022:	f640 6050 	movw	r0, #3664	@ 0xe50
a0a00026:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0002a:	f000 fd63 	bl	a0a00af4 <cfw_log>
a0a0002e:	f000 f969 	bl	a0a00304 <ui_init>
a0a00032:	f640 608c 	movw	r0, #3724	@ 0xe8c
a0a00036:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0003a:	f000 fd5b 	bl	a0a00af4 <cfw_log>
a0a0003e:	f000 fdab 	bl	a0a00b98 <hooks_init>
a0a00042:	f640 60c8 	movw	r0, #3784	@ 0xec8
a0a00046:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0004a:	f000 fd53 	bl	a0a00af4 <cfw_log>
a0a0004e:	2000      	movs	r0, #0
a0a00050:	bd08      	pop	{r3, pc}
a0a00052:	bf00      	nop

a0a00054 <render_toggle>:
a0a00054:	b5f0      	push	{r4, r5, r6, r7, lr}
a0a00056:	4604      	mov	r4, r0
a0a00058:	9805      	ldr	r0, [sp, #20]
a0a0005a:	f89d 501b 	ldrb.w	r5, [sp, #27]
a0a0005e:	4617      	mov	r7, r2
a0a00060:	f640 7e20 	movw	lr, #3872	@ 0xf20
a0a00064:	f2ca 0ea0 	movt	lr, #41120	@ 0xa0a0
a0a00068:	f104 020a 	add.w	r2, r4, #10
a0a0006c:	46a4      	mov	ip, r4
a0a0006e:	460e      	mov	r6, r1
a0a00070:	4619      	mov	r1, r3
a0a00072:	2367      	movs	r3, #103	@ 0x67
a0a00074:	f80c 3b01 	strb.w	r3, [ip], #1
a0a00078:	f81e 3f01 	ldrb.w	r3, [lr, #1]!
a0a0007c:	4562      	cmp	r2, ip
a0a0007e:	d1f9      	bne.n	a0a00074 <render_toggle+0x20>
a0a00080:	2320      	movs	r3, #32
a0a00082:	7423      	strb	r3, [r4, #16]
a0a00084:	f248 531f 	movw	r3, #34079	@ 0x851f
a0a00088:	f2c5 13eb 	movt	r3, #20971	@ 0x51eb
a0a0008c:	42b7      	cmp	r7, r6
a0a0008e:	bf0c      	ite	eq
a0a00090:	223e      	moveq	r2, #62	@ 0x3e
a0a00092:	2220      	movne	r2, #32
a0a00094:	73e2      	strb	r2, [r4, #15]
a0a00096:	fba3 2301 	umull	r2, r3, r3, r1
a0a0009a:	2264      	movs	r2, #100	@ 0x64
a0a0009c:	095b      	lsrs	r3, r3, #5
a0a0009e:	fb02 1213 	mls	r2, r2, r3, r1
a0a000a2:	3330      	adds	r3, #48	@ 0x30
a0a000a4:	72a3      	strb	r3, [r4, #10]
a0a000a6:	f64c 43cd 	movw	r3, #52429	@ 0xcccd
a0a000aa:	f6cc 43cc 	movt	r3, #52428	@ 0xcccc
a0a000ae:	fba3 3202 	umull	r3, r2, r3, r2
a0a000b2:	f243 0320 	movw	r3, #12320	@ 0x3020
a0a000b6:	81a3      	strh	r3, [r4, #12]
a0a000b8:	2322      	movs	r3, #34	@ 0x22
a0a000ba:	73a3      	strb	r3, [r4, #14]
a0a000bc:	f104 0311 	add.w	r3, r4, #17
a0a000c0:	08d2      	lsrs	r2, r2, #3
a0a000c2:	3230      	adds	r2, #48	@ 0x30
a0a000c4:	72e2      	strb	r2, [r4, #11]
a0a000c6:	7802      	ldrb	r2, [r0, #0]
a0a000c8:	b132      	cbz	r2, a0a000d8 <render_toggle+0x84>
a0a000ca:	4601      	mov	r1, r0
a0a000cc:	f803 2b01 	strb.w	r2, [r3], #1
a0a000d0:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a000d4:	2a00      	cmp	r2, #0
a0a000d6:	d1f9      	bne.n	a0a000cc <render_toggle+0x78>
a0a000d8:	f640 7118 	movw	r1, #3864	@ 0xf18
a0a000dc:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a000e0:	f640 7210 	movw	r2, #3856	@ 0xf10
a0a000e4:	f2ca 02a0 	movt	r2, #41120	@ 0xa0a0
a0a000e8:	2d00      	cmp	r5, #0
a0a000ea:	bf08      	it	eq
a0a000ec:	4611      	moveq	r1, r2
a0a000ee:	2220      	movs	r2, #32
a0a000f0:	f803 2b01 	strb.w	r2, [r3], #1
a0a000f4:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a000f8:	2a00      	cmp	r2, #0
a0a000fa:	d1f9      	bne.n	a0a000f0 <render_toggle+0x9c>
a0a000fc:	701a      	strb	r2, [r3, #0]
a0a000fe:	4620      	mov	r0, r4
a0a00100:	f24a 2175 	movw	r1, #41589	@ 0xa275
a0a00104:	f2ca 0102 	movt	r1, #40962	@ 0xa002
a0a00108:	e8bd 40f0 	ldmia.w	sp!, {r4, r5, r6, r7, lr}
a0a0010c:	4708      	bx	r1
a0a0010e:	bf00      	nop

a0a00110 <ui_render.part.0>:
a0a00110:	b530      	push	{r4, r5, lr}
a0a00112:	f640 70b4 	movw	r0, #4020	@ 0xfb4
a0a00116:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0011a:	b0a3      	sub	sp, #140	@ 0x8c
a0a0011c:	f24a 2575 	movw	r5, #41589	@ 0xa275
a0a00120:	f2ca 0502 	movt	r5, #40962	@ 0xa002
a0a00124:	f241 64fc 	movw	r4, #5884	@ 0x16fc
a0a00128:	f2ca 04a0 	movt	r4, #41120	@ 0xa0a0
a0a0012c:	47a8      	blx	r5
a0a0012e:	f640 70c0 	movw	r0, #4032	@ 0xfc0
a0a00132:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00136:	47a8      	blx	r5
a0a00138:	f640 70d4 	movw	r0, #4052	@ 0xfd4
a0a0013c:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00140:	47a8      	blx	r5
a0a00142:	7863      	ldrb	r3, [r4, #1]
a0a00144:	2b00      	cmp	r3, #0
a0a00146:	f040 808e 	bne.w	a0a00266 <ui_render.part.0+0x156>
a0a0014a:	f640 70e8 	movw	r0, #4072	@ 0xfe8
a0a0014e:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00152:	47a8      	blx	r5
a0a00154:	f241 0050 	movw	r0, #4176	@ 0x1050
a0a00158:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0015c:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a00160:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00164:	4798      	blx	r3
a0a00166:	7863      	ldrb	r3, [r4, #1]
a0a00168:	6862      	ldr	r2, [r4, #4]
a0a0016a:	2b00      	cmp	r3, #0
a0a0016c:	f040 8087 	bne.w	a0a0027e <ui_render.part.0+0x16e>
a0a00170:	f640 712c 	movw	r1, #3884	@ 0xf2c
a0a00174:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a00178:	f640 734c 	movw	r3, #3916	@ 0xf4c
a0a0017c:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a00180:	6920      	ldr	r0, [r4, #16]
a0a00182:	2a00      	cmp	r2, #0
a0a00184:	bf18      	it	ne
a0a00186:	4619      	movne	r1, r3
a0a00188:	ab02      	add	r3, sp, #8
a0a0018a:	2267      	movs	r2, #103	@ 0x67
a0a0018c:	f803 2b01 	strb.w	r2, [r3], #1
a0a00190:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00194:	2a00      	cmp	r2, #0
a0a00196:	d1f9      	bne.n	a0a0018c <ui_render.part.0+0x7c>
a0a00198:	f241 52bc 	movw	r2, #5564	@ 0x15bc
a0a0019c:	f2ca 02a0 	movt	r2, #41120	@ 0xa0a0
a0a001a0:	2128      	movs	r1, #40	@ 0x28
a0a001a2:	fb01 2200 	mla	r2, r1, r0, r2
a0a001a6:	6851      	ldr	r1, [r2, #4]
a0a001a8:	780a      	ldrb	r2, [r1, #0]
a0a001aa:	b12a      	cbz	r2, a0a001b8 <ui_render.part.0+0xa8>
a0a001ac:	f803 2b01 	strb.w	r2, [r3], #1
a0a001b0:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a001b4:	2a00      	cmp	r2, #0
a0a001b6:	d1f9      	bne.n	a0a001ac <ui_render.part.0+0x9c>
a0a001b8:	a802      	add	r0, sp, #8
a0a001ba:	f44f 5208 	mov.w	r2, #8704	@ 0x2200
a0a001be:	f2c2 023e 	movt	r2, #8254	@ 0x203e
a0a001c2:	601a      	str	r2, [r3, #0]
a0a001c4:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a001c8:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a001cc:	4798      	blx	r3
a0a001ce:	6862      	ldr	r2, [r4, #4]
a0a001d0:	f640 716c 	movw	r1, #3948	@ 0xf6c
a0a001d4:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a001d8:	f640 738c 	movw	r3, #3980	@ 0xf8c
a0a001dc:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a001e0:	2a01      	cmp	r2, #1
a0a001e2:	bf18      	it	ne
a0a001e4:	4619      	movne	r1, r3
a0a001e6:	ab02      	add	r3, sp, #8
a0a001e8:	2267      	movs	r2, #103	@ 0x67
a0a001ea:	f803 2b01 	strb.w	r2, [r3], #1
a0a001ee:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a001f2:	2a00      	cmp	r2, #0
a0a001f4:	d1f9      	bne.n	a0a001ea <ui_render.part.0+0xda>
a0a001f6:	7d22      	ldrb	r2, [r4, #20]
a0a001f8:	2a06      	cmp	r2, #6
a0a001fa:	d83a      	bhi.n	a0a00272 <ui_render.part.0+0x162>
a0a001fc:	f241 51a0 	movw	r1, #5536	@ 0x15a0
a0a00200:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a00204:	f851 1022 	ldr.w	r1, [r1, r2, lsl #2]
a0a00208:	780a      	ldrb	r2, [r1, #0]
a0a0020a:	b12a      	cbz	r2, a0a00218 <ui_render.part.0+0x108>
a0a0020c:	f803 2b01 	strb.w	r2, [r3], #1
a0a00210:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00214:	2a00      	cmp	r2, #0
a0a00216:	d1f9      	bne.n	a0a0020c <ui_render.part.0+0xfc>
a0a00218:	a802      	add	r0, sp, #8
a0a0021a:	f44f 5208 	mov.w	r2, #8704	@ 0x2200
a0a0021e:	f2c2 023e 	movt	r2, #8254	@ 0x203e
a0a00222:	f24a 2575 	movw	r5, #41589	@ 0xa275
a0a00226:	f2ca 0502 	movt	r5, #40962	@ 0xa002
a0a0022a:	601a      	str	r2, [r3, #0]
a0a0022c:	47a8      	blx	r5
a0a0022e:	6863      	ldr	r3, [r4, #4]
a0a00230:	2b02      	cmp	r3, #2
a0a00232:	bf0b      	itete	eq
a0a00234:	f241 0064 	movweq	r0, #4196	@ 0x1064
a0a00238:	f241 0094 	movwne	r0, #4244	@ 0x1094
a0a0023c:	f2ca 00a0 	movteq	r0, #41120	@ 0xa0a0
a0a00240:	f2ca 00a0 	movtne	r0, #41120	@ 0xa0a0
a0a00244:	47a8      	blx	r5
a0a00246:	f241 104c 	movw	r0, #4428	@ 0x114c
a0a0024a:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0024e:	f24a 2475 	movw	r4, #41589	@ 0xa275
a0a00252:	f2ca 0402 	movt	r4, #40962	@ 0xa002
a0a00256:	47a0      	blx	r4
a0a00258:	f241 1064 	movw	r0, #4452	@ 0x1164
a0a0025c:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00260:	47a0      	blx	r4
a0a00262:	b023      	add	sp, #140	@ 0x8c
a0a00264:	bd30      	pop	{r4, r5, pc}
a0a00266:	f241 001c 	movw	r0, #4124	@ 0x101c
a0a0026a:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0026e:	47a8      	blx	r5
a0a00270:	e770      	b.n	a0a00154 <ui_render.part.0+0x44>
a0a00272:	f640 71ac 	movw	r1, #4012	@ 0xfac
a0a00276:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a0027a:	2255      	movs	r2, #85	@ 0x55
a0a0027c:	e7c6      	b.n	a0a0020c <ui_render.part.0+0xfc>
a0a0027e:	7a23      	ldrb	r3, [r4, #8]
a0a00280:	a802      	add	r0, sp, #8
a0a00282:	9301      	str	r3, [sp, #4]
a0a00284:	f241 01c0 	movw	r1, #4288	@ 0x10c0
a0a00288:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a0028c:	236e      	movs	r3, #110	@ 0x6e
a0a0028e:	9100      	str	r1, [sp, #0]
a0a00290:	2100      	movs	r1, #0
a0a00292:	f7ff fedf 	bl	a0a00054 <render_toggle>
a0a00296:	7a63      	ldrb	r3, [r4, #9]
a0a00298:	6862      	ldr	r2, [r4, #4]
a0a0029a:	a802      	add	r0, sp, #8
a0a0029c:	9301      	str	r3, [sp, #4]
a0a0029e:	f241 01dc 	movw	r1, #4316	@ 0x10dc
a0a002a2:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a002a6:	2396      	movs	r3, #150	@ 0x96
a0a002a8:	9100      	str	r1, [sp, #0]
a0a002aa:	2101      	movs	r1, #1
a0a002ac:	f7ff fed2 	bl	a0a00054 <render_toggle>
a0a002b0:	7aa3      	ldrb	r3, [r4, #10]
a0a002b2:	6862      	ldr	r2, [r4, #4]
a0a002b4:	a802      	add	r0, sp, #8
a0a002b6:	9301      	str	r3, [sp, #4]
a0a002b8:	f241 01f8 	movw	r1, #4344	@ 0x10f8
a0a002bc:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a002c0:	23be      	movs	r3, #190	@ 0xbe
a0a002c2:	9100      	str	r1, [sp, #0]
a0a002c4:	2102      	movs	r1, #2
a0a002c6:	f7ff fec5 	bl	a0a00054 <render_toggle>
a0a002ca:	7ae3      	ldrb	r3, [r4, #11]
a0a002cc:	6862      	ldr	r2, [r4, #4]
a0a002ce:	a802      	add	r0, sp, #8
a0a002d0:	9301      	str	r3, [sp, #4]
a0a002d2:	f241 1114 	movw	r1, #4372	@ 0x1114
a0a002d6:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a002da:	23e6      	movs	r3, #230	@ 0xe6
a0a002dc:	9100      	str	r1, [sp, #0]
a0a002de:	2103      	movs	r1, #3
a0a002e0:	f7ff feb8 	bl	a0a00054 <render_toggle>
a0a002e4:	7b21      	ldrb	r1, [r4, #12]
a0a002e6:	6862      	ldr	r2, [r4, #4]
a0a002e8:	a802      	add	r0, sp, #8
a0a002ea:	f241 1330 	movw	r3, #4400	@ 0x1130
a0a002ee:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a002f2:	e9cd 3100 	strd	r3, r1, [sp]
a0a002f6:	f44f 7387 	mov.w	r3, #270	@ 0x10e
a0a002fa:	2104      	movs	r1, #4
a0a002fc:	f7ff feaa 	bl	a0a00054 <render_toggle>
a0a00300:	e7a1      	b.n	a0a00246 <ui_render.part.0+0x136>
a0a00302:	bf00      	nop

a0a00304 <ui_init>:
a0a00304:	f241 63fc 	movw	r3, #5884	@ 0x16fc
a0a00308:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a0030c:	2200      	movs	r2, #0
a0a0030e:	801a      	strh	r2, [r3, #0]
a0a00310:	e9c3 2201 	strd	r2, r2, [r3, #4]
a0a00314:	731a      	strb	r2, [r3, #12]
a0a00316:	611a      	str	r2, [r3, #16]
a0a00318:	751a      	strb	r2, [r3, #20]
a0a0031a:	4770      	bx	lr

a0a0031c <ui_toggle_menu>:
a0a0031c:	b510      	push	{r4, lr}
a0a0031e:	f241 64fc 	movw	r4, #5884	@ 0x16fc
a0a00322:	f2ca 04a0 	movt	r4, #41120	@ 0xa0a0
a0a00326:	7823      	ldrb	r3, [r4, #0]
a0a00328:	f083 0301 	eor.w	r3, r3, #1
a0a0032c:	7023      	strb	r3, [r4, #0]
a0a0032e:	b153      	cbz	r3, a0a00346 <ui_toggle_menu+0x2a>
a0a00330:	f241 10ac 	movw	r0, #4524	@ 0x11ac
a0a00334:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00338:	f000 fbdc 	bl	a0a00af4 <cfw_log>
a0a0033c:	7823      	ldrb	r3, [r4, #0]
a0a0033e:	b15b      	cbz	r3, a0a00358 <ui_toggle_menu+0x3c>
a0a00340:	e8bd 4010 	ldmia.w	sp!, {r4, lr}
a0a00344:	e6e4      	b.n	a0a00110 <ui_render.part.0>
a0a00346:	f241 10d0 	movw	r0, #4560	@ 0x11d0
a0a0034a:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0034e:	f000 fbd1 	bl	a0a00af4 <cfw_log>
a0a00352:	7823      	ldrb	r3, [r4, #0]
a0a00354:	2b00      	cmp	r3, #0
a0a00356:	d1f3      	bne.n	a0a00340 <ui_toggle_menu+0x24>
a0a00358:	f241 10f4 	movw	r0, #4596	@ 0x11f4
a0a0035c:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00360:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a00364:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00368:	e8bd 4010 	ldmia.w	sp!, {r4, lr}
a0a0036c:	4718      	bx	r3
a0a0036e:	bf00      	nop

a0a00370 <ui_action_ok.part.0>:
a0a00370:	f241 63fc 	movw	r3, #5884	@ 0x16fc
a0a00374:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a00378:	b500      	push	{lr}
a0a0037a:	7859      	ldrb	r1, [r3, #1]
a0a0037c:	b083      	sub	sp, #12
a0a0037e:	685a      	ldr	r2, [r3, #4]
a0a00380:	b939      	cbnz	r1, a0a00392 <ui_action_ok.part.0+0x22>
a0a00382:	2a02      	cmp	r2, #2
a0a00384:	d055      	beq.n	a0a00432 <ui_action_ok.part.0+0xc2>
a0a00386:	781b      	ldrb	r3, [r3, #0]
a0a00388:	b153      	cbz	r3, a0a003a0 <ui_action_ok.part.0+0x30>
a0a0038a:	b003      	add	sp, #12
a0a0038c:	f85d eb04 	ldr.w	lr, [sp], #4
a0a00390:	e6be      	b.n	a0a00110 <ui_render.part.0>
a0a00392:	2a04      	cmp	r2, #4
a0a00394:	d8f7      	bhi.n	a0a00386 <ui_action_ok.part.0+0x16>
a0a00396:	e8df f002 	tbb	[pc, r2]
a0a0039a:	2831      	.short	0x2831
a0a0039c:	3a43      	.short	0x3a43
a0a0039e:	0f          	.byte	0x0f
a0a0039f:	00          	.byte	0x00
a0a003a0:	f241 10f4 	movw	r0, #4596	@ 0x11f4
a0a003a4:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a003a8:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a003ac:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a003b0:	b003      	add	sp, #12
a0a003b2:	f85d eb04 	ldr.w	lr, [sp], #4
a0a003b6:	4718      	bx	r3
a0a003b8:	7b1a      	ldrb	r2, [r3, #12]
a0a003ba:	f082 0201 	eor.w	r2, r2, #1
a0a003be:	731a      	strb	r2, [r3, #12]
a0a003c0:	2a00      	cmp	r2, #0
a0a003c2:	d04f      	beq.n	a0a00464 <ui_action_ok.part.0+0xf4>
a0a003c4:	9301      	str	r3, [sp, #4]
a0a003c6:	f241 10fc 	movw	r0, #4604	@ 0x11fc
a0a003ca:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a003ce:	f24a 2275 	movw	r2, #41589	@ 0xa275
a0a003d2:	f2ca 0202 	movt	r2, #40962	@ 0xa002
a0a003d6:	9200      	str	r2, [sp, #0]
a0a003d8:	4790      	blx	r2
a0a003da:	9a00      	ldr	r2, [sp, #0]
a0a003dc:	f241 2010 	movw	r0, #4624	@ 0x1210
a0a003e0:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a003e4:	4790      	blx	r2
a0a003e6:	9b01      	ldr	r3, [sp, #4]
a0a003e8:	e7cd      	b.n	a0a00386 <ui_action_ok.part.0+0x16>
a0a003ea:	7a18      	ldrb	r0, [r3, #8]
a0a003ec:	9300      	str	r3, [sp, #0]
a0a003ee:	f080 0001 	eor.w	r0, r0, #1
a0a003f2:	7218      	strb	r0, [r3, #8]
a0a003f4:	f000 fbfe 	bl	a0a00bf4 <hooks_set_clarity_slider>
a0a003f8:	9b00      	ldr	r3, [sp, #0]
a0a003fa:	e7c4      	b.n	a0a00386 <ui_action_ok.part.0+0x16>
a0a003fc:	7a58      	ldrb	r0, [r3, #9]
a0a003fe:	9300      	str	r3, [sp, #0]
a0a00400:	f080 0001 	eor.w	r0, r0, #1
a0a00404:	7258      	strb	r0, [r3, #9]
a0a00406:	f000 fc39 	bl	a0a00c7c <hooks_set_scurve_contrast>
a0a0040a:	9b00      	ldr	r3, [sp, #0]
a0a0040c:	e7bb      	b.n	a0a00386 <ui_action_ok.part.0+0x16>
a0a0040e:	7a98      	ldrb	r0, [r3, #10]
a0a00410:	9300      	str	r3, [sp, #0]
a0a00412:	f080 0001 	eor.w	r0, r0, #1
a0a00416:	7298      	strb	r0, [r3, #10]
a0a00418:	f000 fc44 	bl	a0a00ca4 <hooks_set_auto_iso_floor>
a0a0041c:	9b00      	ldr	r3, [sp, #0]
a0a0041e:	e7b2      	b.n	a0a00386 <ui_action_ok.part.0+0x16>
a0a00420:	7ad8      	ldrb	r0, [r3, #11]
a0a00422:	9300      	str	r3, [sp, #0]
a0a00424:	f080 0001 	eor.w	r0, r0, #1
a0a00428:	72d8      	strb	r0, [r3, #11]
a0a0042a:	f000 fc4f 	bl	a0a00ccc <hooks_set_fine_grain_nr>
a0a0042e:	9b00      	ldr	r3, [sp, #0]
a0a00430:	e7a9      	b.n	a0a00386 <ui_action_ok.part.0+0x16>
a0a00432:	6918      	ldr	r0, [r3, #16]
a0a00434:	f241 52bc 	movw	r2, #5564	@ 0x15bc
a0a00438:	f2ca 02a0 	movt	r2, #41120	@ 0xa0a0
a0a0043c:	f04f 0c28 	mov.w	ip, #40	@ 0x28
a0a00440:	9301      	str	r3, [sp, #4]
a0a00442:	fb0c 2200 	mla	r2, ip, r0, r2
a0a00446:	9200      	str	r2, [sp, #0]
a0a00448:	4610      	mov	r0, r2
a0a0044a:	f000 f891 	bl	a0a00570 <film_apply>
a0a0044e:	9a00      	ldr	r2, [sp, #0]
a0a00450:	9b01      	ldr	r3, [sp, #4]
a0a00452:	6851      	ldr	r1, [r2, #4]
a0a00454:	7d18      	ldrb	r0, [r3, #20]
a0a00456:	f000 fc4d 	bl	a0a00cf4 <hooks_patch_slot_name>
a0a0045a:	b003      	add	sp, #12
a0a0045c:	f85d eb04 	ldr.w	lr, [sp], #4
a0a00460:	f7ff bf5c 	b.w	a0a0031c <ui_toggle_menu>
a0a00464:	9300      	str	r3, [sp, #0]
a0a00466:	f241 10f4 	movw	r0, #4596	@ 0x11f4
a0a0046a:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a0046e:	f24a 2275 	movw	r2, #41589	@ 0xa275
a0a00472:	f2ca 0202 	movt	r2, #40962	@ 0xa002
a0a00476:	4790      	blx	r2
a0a00478:	9b00      	ldr	r3, [sp, #0]
a0a0047a:	e784      	b.n	a0a00386 <ui_action_ok.part.0+0x16>

a0a0047c <ui_nav_up>:
a0a0047c:	f241 63fc 	movw	r3, #5884	@ 0x16fc
a0a00480:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a00484:	781a      	ldrb	r2, [r3, #0]
a0a00486:	b15a      	cbz	r2, a0a004a0 <ui_nav_up+0x24>
a0a00488:	7859      	ldrb	r1, [r3, #1]
a0a0048a:	685a      	ldr	r2, [r3, #4]
a0a0048c:	2900      	cmp	r1, #0
a0a0048e:	bf14      	ite	ne
a0a00490:	2105      	movne	r1, #5
a0a00492:	2103      	moveq	r1, #3
a0a00494:	3a01      	subs	r2, #1
a0a00496:	bf48      	it	mi
a0a00498:	f101 32ff 	addmi.w	r2, r1, #4294967295	@ 0xffffffff
a0a0049c:	605a      	str	r2, [r3, #4]
a0a0049e:	e637      	b.n	a0a00110 <ui_render.part.0>
a0a004a0:	4770      	bx	lr
a0a004a2:	bf00      	nop

a0a004a4 <ui_nav_down>:
a0a004a4:	f241 63fc 	movw	r3, #5884	@ 0x16fc
a0a004a8:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a004ac:	781a      	ldrb	r2, [r3, #0]
a0a004ae:	b162      	cbz	r2, a0a004ca <ui_nav_down+0x26>
a0a004b0:	7859      	ldrb	r1, [r3, #1]
a0a004b2:	685a      	ldr	r2, [r3, #4]
a0a004b4:	2900      	cmp	r1, #0
a0a004b6:	f102 0201 	add.w	r2, r2, #1
a0a004ba:	bf14      	ite	ne
a0a004bc:	2105      	movne	r1, #5
a0a004be:	2103      	moveq	r1, #3
a0a004c0:	428a      	cmp	r2, r1
a0a004c2:	bfa8      	it	ge
a0a004c4:	2200      	movge	r2, #0
a0a004c6:	605a      	str	r2, [r3, #4]
a0a004c8:	e622      	b.n	a0a00110 <ui_render.part.0>
a0a004ca:	4770      	bx	lr

a0a004cc <ui_nav_left>:
a0a004cc:	f241 63fc 	movw	r3, #5884	@ 0x16fc
a0a004d0:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a004d4:	781a      	ldrb	r2, [r3, #0]
a0a004d6:	b192      	cbz	r2, a0a004fe <ui_nav_left+0x32>
a0a004d8:	785a      	ldrb	r2, [r3, #1]
a0a004da:	b98a      	cbnz	r2, a0a00500 <ui_nav_left+0x34>
a0a004dc:	685a      	ldr	r2, [r3, #4]
a0a004de:	b93a      	cbnz	r2, a0a004f0 <ui_nav_left+0x24>
a0a004e0:	691a      	ldr	r2, [r3, #16]
a0a004e2:	3a01      	subs	r2, #1
a0a004e4:	bf44      	itt	mi
a0a004e6:	2207      	movmi	r2, #7
a0a004e8:	611a      	strmi	r2, [r3, #16]
a0a004ea:	d400      	bmi.n	a0a004ee <ui_nav_left+0x22>
a0a004ec:	611a      	str	r2, [r3, #16]
a0a004ee:	e60f      	b.n	a0a00110 <ui_render.part.0>
a0a004f0:	2a01      	cmp	r2, #1
a0a004f2:	d1fc      	bne.n	a0a004ee <ui_nav_left+0x22>
a0a004f4:	7d1a      	ldrb	r2, [r3, #20]
a0a004f6:	b122      	cbz	r2, a0a00502 <ui_nav_left+0x36>
a0a004f8:	3a01      	subs	r2, #1
a0a004fa:	751a      	strb	r2, [r3, #20]
a0a004fc:	e7f7      	b.n	a0a004ee <ui_nav_left+0x22>
a0a004fe:	4770      	bx	lr
a0a00500:	e736      	b.n	a0a00370 <ui_action_ok.part.0>
a0a00502:	2206      	movs	r2, #6
a0a00504:	751a      	strb	r2, [r3, #20]
a0a00506:	e7f2      	b.n	a0a004ee <ui_nav_left+0x22>

a0a00508 <ui_nav_right>:
a0a00508:	f241 63fc 	movw	r3, #5884	@ 0x16fc
a0a0050c:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a00510:	781a      	ldrb	r2, [r3, #0]
a0a00512:	b19a      	cbz	r2, a0a0053c <ui_nav_right+0x34>
a0a00514:	785a      	ldrb	r2, [r3, #1]
a0a00516:	b992      	cbnz	r2, a0a0053e <ui_nav_right+0x36>
a0a00518:	6859      	ldr	r1, [r3, #4]
a0a0051a:	b931      	cbnz	r1, a0a0052a <ui_nav_right+0x22>
a0a0051c:	691a      	ldr	r2, [r3, #16]
a0a0051e:	3201      	adds	r2, #1
a0a00520:	2a07      	cmp	r2, #7
a0a00522:	bfd8      	it	le
a0a00524:	4611      	movle	r1, r2
a0a00526:	6119      	str	r1, [r3, #16]
a0a00528:	e5f2      	b.n	a0a00110 <ui_render.part.0>
a0a0052a:	2901      	cmp	r1, #1
a0a0052c:	d1fc      	bne.n	a0a00528 <ui_nav_right+0x20>
a0a0052e:	7d19      	ldrb	r1, [r3, #20]
a0a00530:	3101      	adds	r1, #1
a0a00532:	2906      	cmp	r1, #6
a0a00534:	bfd8      	it	le
a0a00536:	460a      	movle	r2, r1
a0a00538:	751a      	strb	r2, [r3, #20]
a0a0053a:	e7f5      	b.n	a0a00528 <ui_nav_right+0x20>
a0a0053c:	4770      	bx	lr
a0a0053e:	e717      	b.n	a0a00370 <ui_action_ok.part.0>

a0a00540 <ui_action_ok>:
a0a00540:	f241 63fc 	movw	r3, #5884	@ 0x16fc
a0a00544:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a00548:	781b      	ldrb	r3, [r3, #0]
a0a0054a:	b103      	cbz	r3, a0a0054e <ui_action_ok+0xe>
a0a0054c:	e710      	b.n	a0a00370 <ui_action_ok.part.0>
a0a0054e:	4770      	bx	lr

a0a00550 <ui_render>:
a0a00550:	f241 63fc 	movw	r3, #5884	@ 0x16fc
a0a00554:	f2ca 03a0 	movt	r3, #41120	@ 0xa0a0
a0a00558:	781b      	ldrb	r3, [r3, #0]
a0a0055a:	b103      	cbz	r3, a0a0055e <ui_render+0xe>
a0a0055c:	e5d8      	b.n	a0a00110 <ui_render.part.0>
a0a0055e:	f241 10f4 	movw	r0, #4596	@ 0x11f4
a0a00562:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00566:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a0056a:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a0056e:	4718      	bx	r3

a0a00570 <film_apply>:
a0a00570:	2800      	cmp	r0, #0
a0a00572:	f000 82bb 	beq.w	a0a00aec <film_apply+0x57c>
a0a00576:	b510      	push	{r4, lr}
a0a00578:	f241 2280 	movw	r2, #4736	@ 0x1280
a0a0057c:	f2ca 02a0 	movt	r2, #41120	@ 0xa0a0
a0a00580:	b0a2      	sub	sp, #136	@ 0x88
a0a00582:	f04f 0c6d 	mov.w	ip, #109	@ 0x6d
a0a00586:	ab02      	add	r3, sp, #8
a0a00588:	f10d 0e19 	add.w	lr, sp, #25
a0a0058c:	f803 cb01 	strb.w	ip, [r3], #1
a0a00590:	f812 cf01 	ldrb.w	ip, [r2, #1]!
a0a00594:	4573      	cmp	r3, lr
a0a00596:	d1f9      	bne.n	a0a0058c <film_apply+0x1c>
a0a00598:	f9b0 200c 	ldrsh.w	r2, [r0, #12]
a0a0059c:	2a00      	cmp	r2, #0
a0a0059e:	bfbf      	itttt	lt
a0a005a0:	4252      	neglt	r2, r2
a0a005a2:	232d      	movlt	r3, #45	@ 0x2d
a0a005a4:	f88d 3019 	strblt.w	r3, [sp, #25]
a0a005a8:	f10d 031a 	addlt.w	r3, sp, #26
a0a005ac:	2a63      	cmp	r2, #99	@ 0x63
a0a005ae:	f300 81e4 	bgt.w	a0a0097a <film_apply+0x40a>
a0a005b2:	461c      	mov	r4, r3
a0a005b4:	2a09      	cmp	r2, #9
a0a005b6:	dd0d      	ble.n	a0a005d4 <film_apply+0x64>
a0a005b8:	f64c 43cd 	movw	r3, #52429	@ 0xcccd
a0a005bc:	f6cc 43cc 	movt	r3, #52428	@ 0xcccc
a0a005c0:	fba3 c302 	umull	ip, r3, r3, r2
a0a005c4:	f04f 0c0a 	mov.w	ip, #10
a0a005c8:	08db      	lsrs	r3, r3, #3
a0a005ca:	fb0c 2213 	mls	r2, ip, r3, r2
a0a005ce:	3330      	adds	r3, #48	@ 0x30
a0a005d0:	f804 3b01 	strb.w	r3, [r4], #1
a0a005d4:	232c      	movs	r3, #44	@ 0x2c
a0a005d6:	7063      	strb	r3, [r4, #1]
a0a005d8:	f9b0 300e 	ldrsh.w	r3, [r0, #14]
a0a005dc:	3230      	adds	r2, #48	@ 0x30
a0a005de:	7022      	strb	r2, [r4, #0]
a0a005e0:	2b00      	cmp	r3, #0
a0a005e2:	bfb7      	itett	lt
a0a005e4:	425b      	neglt	r3, r3
a0a005e6:	f104 0c02 	addge.w	ip, r4, #2
a0a005ea:	222d      	movlt	r2, #45	@ 0x2d
a0a005ec:	f104 0c03 	addlt.w	ip, r4, #3
a0a005f0:	bfb8      	it	lt
a0a005f2:	70a2      	strblt	r2, [r4, #2]
a0a005f4:	2b63      	cmp	r3, #99	@ 0x63
a0a005f6:	f340 8218 	ble.w	a0a00a2a <film_apply+0x4ba>
a0a005fa:	f248 521f 	movw	r2, #34079	@ 0x851f
a0a005fe:	f2c5 12eb 	movt	r2, #20971	@ 0x51eb
a0a00602:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a00606:	fba2 4203 	umull	r4, r2, r2, r3
a0a0060a:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a0060e:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00612:	0952      	lsrs	r2, r2, #5
a0a00614:	fb0e 3e12 	mls	lr, lr, r2, r3
a0a00618:	f102 0330 	add.w	r3, r2, #48	@ 0x30
a0a0061c:	fba4 240e 	umull	r2, r4, r4, lr
a0a00620:	4662      	mov	r2, ip
a0a00622:	08e4      	lsrs	r4, r4, #3
a0a00624:	f802 3b02 	strb.w	r3, [r2], #2
a0a00628:	230a      	movs	r3, #10
a0a0062a:	fb03 e314 	mls	r3, r3, r4, lr
a0a0062e:	3430      	adds	r4, #48	@ 0x30
a0a00630:	f88c 4001 	strb.w	r4, [ip, #1]
a0a00634:	3330      	adds	r3, #48	@ 0x30
a0a00636:	7013      	strb	r3, [r2, #0]
a0a00638:	232c      	movs	r3, #44	@ 0x2c
a0a0063a:	7053      	strb	r3, [r2, #1]
a0a0063c:	f9b0 3010 	ldrsh.w	r3, [r0, #16]
a0a00640:	2b00      	cmp	r3, #0
a0a00642:	bfb7      	itett	lt
a0a00644:	425b      	neglt	r3, r3
a0a00646:	f102 0c02 	addge.w	ip, r2, #2
a0a0064a:	242d      	movlt	r4, #45	@ 0x2d
a0a0064c:	f102 0c03 	addlt.w	ip, r2, #3
a0a00650:	bfb8      	it	lt
a0a00652:	7094      	strblt	r4, [r2, #2]
a0a00654:	2b63      	cmp	r3, #99	@ 0x63
a0a00656:	f340 81d5 	ble.w	a0a00a04 <film_apply+0x494>
a0a0065a:	f248 521f 	movw	r2, #34079	@ 0x851f
a0a0065e:	f2c5 12eb 	movt	r2, #20971	@ 0x51eb
a0a00662:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a00666:	fba2 4203 	umull	r4, r2, r2, r3
a0a0066a:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a0066e:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00672:	0952      	lsrs	r2, r2, #5
a0a00674:	fb0e 3e12 	mls	lr, lr, r2, r3
a0a00678:	f102 0330 	add.w	r3, r2, #48	@ 0x30
a0a0067c:	fba4 240e 	umull	r2, r4, r4, lr
a0a00680:	4662      	mov	r2, ip
a0a00682:	08e4      	lsrs	r4, r4, #3
a0a00684:	f802 3b02 	strb.w	r3, [r2], #2
a0a00688:	230a      	movs	r3, #10
a0a0068a:	fb03 e314 	mls	r3, r3, r4, lr
a0a0068e:	3430      	adds	r4, #48	@ 0x30
a0a00690:	f88c 4001 	strb.w	r4, [ip, #1]
a0a00694:	3330      	adds	r3, #48	@ 0x30
a0a00696:	7013      	strb	r3, [r2, #0]
a0a00698:	2329      	movs	r3, #41	@ 0x29
a0a0069a:	7053      	strb	r3, [r2, #1]
a0a0069c:	2320      	movs	r3, #32
a0a0069e:	7093      	strb	r3, [r2, #2]
a0a006a0:	2328      	movs	r3, #40	@ 0x28
a0a006a2:	70d3      	strb	r3, [r2, #3]
a0a006a4:	f9b0 3012 	ldrsh.w	r3, [r0, #18]
a0a006a8:	2b00      	cmp	r3, #0
a0a006aa:	bfb7      	itett	lt
a0a006ac:	425b      	neglt	r3, r3
a0a006ae:	f102 0c04 	addge.w	ip, r2, #4
a0a006b2:	242d      	movlt	r4, #45	@ 0x2d
a0a006b4:	f102 0c05 	addlt.w	ip, r2, #5
a0a006b8:	bfb8      	it	lt
a0a006ba:	7114      	strblt	r4, [r2, #4]
a0a006bc:	2b63      	cmp	r3, #99	@ 0x63
a0a006be:	f340 8200 	ble.w	a0a00ac2 <film_apply+0x552>
a0a006c2:	f248 521f 	movw	r2, #34079	@ 0x851f
a0a006c6:	f2c5 12eb 	movt	r2, #20971	@ 0x51eb
a0a006ca:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a006ce:	fba2 4203 	umull	r4, r2, r2, r3
a0a006d2:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a006d6:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a006da:	0952      	lsrs	r2, r2, #5
a0a006dc:	fb0e 3e12 	mls	lr, lr, r2, r3
a0a006e0:	f102 0330 	add.w	r3, r2, #48	@ 0x30
a0a006e4:	fba4 240e 	umull	r2, r4, r4, lr
a0a006e8:	4662      	mov	r2, ip
a0a006ea:	08e4      	lsrs	r4, r4, #3
a0a006ec:	f802 3b02 	strb.w	r3, [r2], #2
a0a006f0:	230a      	movs	r3, #10
a0a006f2:	fb03 e314 	mls	r3, r3, r4, lr
a0a006f6:	3430      	adds	r4, #48	@ 0x30
a0a006f8:	f88c 4001 	strb.w	r4, [ip, #1]
a0a006fc:	3330      	adds	r3, #48	@ 0x30
a0a006fe:	7013      	strb	r3, [r2, #0]
a0a00700:	232c      	movs	r3, #44	@ 0x2c
a0a00702:	7053      	strb	r3, [r2, #1]
a0a00704:	f9b0 3014 	ldrsh.w	r3, [r0, #20]
a0a00708:	2b00      	cmp	r3, #0
a0a0070a:	bfb7      	itett	lt
a0a0070c:	425b      	neglt	r3, r3
a0a0070e:	f102 0c02 	addge.w	ip, r2, #2
a0a00712:	242d      	movlt	r4, #45	@ 0x2d
a0a00714:	f102 0c03 	addlt.w	ip, r2, #3
a0a00718:	bfb8      	it	lt
a0a0071a:	7094      	strblt	r4, [r2, #2]
a0a0071c:	2b63      	cmp	r3, #99	@ 0x63
a0a0071e:	f340 81bd 	ble.w	a0a00a9c <film_apply+0x52c>
a0a00722:	f248 521f 	movw	r2, #34079	@ 0x851f
a0a00726:	f2c5 12eb 	movt	r2, #20971	@ 0x51eb
a0a0072a:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a0072e:	fba2 4203 	umull	r4, r2, r2, r3
a0a00732:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a00736:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a0073a:	0952      	lsrs	r2, r2, #5
a0a0073c:	fb0e 3e12 	mls	lr, lr, r2, r3
a0a00740:	f102 0330 	add.w	r3, r2, #48	@ 0x30
a0a00744:	fba4 240e 	umull	r2, r4, r4, lr
a0a00748:	4662      	mov	r2, ip
a0a0074a:	08e4      	lsrs	r4, r4, #3
a0a0074c:	f802 3b02 	strb.w	r3, [r2], #2
a0a00750:	230a      	movs	r3, #10
a0a00752:	fb03 e314 	mls	r3, r3, r4, lr
a0a00756:	3430      	adds	r4, #48	@ 0x30
a0a00758:	f88c 4001 	strb.w	r4, [ip, #1]
a0a0075c:	3330      	adds	r3, #48	@ 0x30
a0a0075e:	7013      	strb	r3, [r2, #0]
a0a00760:	232c      	movs	r3, #44	@ 0x2c
a0a00762:	7053      	strb	r3, [r2, #1]
a0a00764:	f9b0 3016 	ldrsh.w	r3, [r0, #22]
a0a00768:	2b00      	cmp	r3, #0
a0a0076a:	bfb7      	itett	lt
a0a0076c:	425b      	neglt	r3, r3
a0a0076e:	f102 0c02 	addge.w	ip, r2, #2
a0a00772:	242d      	movlt	r4, #45	@ 0x2d
a0a00774:	f102 0c03 	addlt.w	ip, r2, #3
a0a00778:	bfb8      	it	lt
a0a0077a:	7094      	strblt	r4, [r2, #2]
a0a0077c:	2b63      	cmp	r3, #99	@ 0x63
a0a0077e:	f340 817a 	ble.w	a0a00a76 <film_apply+0x506>
a0a00782:	f248 521f 	movw	r2, #34079	@ 0x851f
a0a00786:	f2c5 12eb 	movt	r2, #20971	@ 0x51eb
a0a0078a:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a0078e:	fba2 4203 	umull	r4, r2, r2, r3
a0a00792:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a00796:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a0079a:	0952      	lsrs	r2, r2, #5
a0a0079c:	fb0e 3e12 	mls	lr, lr, r2, r3
a0a007a0:	f102 0330 	add.w	r3, r2, #48	@ 0x30
a0a007a4:	fba4 240e 	umull	r2, r4, r4, lr
a0a007a8:	4662      	mov	r2, ip
a0a007aa:	08e4      	lsrs	r4, r4, #3
a0a007ac:	f802 3b02 	strb.w	r3, [r2], #2
a0a007b0:	230a      	movs	r3, #10
a0a007b2:	fb03 e314 	mls	r3, r3, r4, lr
a0a007b6:	3430      	adds	r4, #48	@ 0x30
a0a007b8:	f88c 4001 	strb.w	r4, [ip, #1]
a0a007bc:	3330      	adds	r3, #48	@ 0x30
a0a007be:	7013      	strb	r3, [r2, #0]
a0a007c0:	2329      	movs	r3, #41	@ 0x29
a0a007c2:	7053      	strb	r3, [r2, #1]
a0a007c4:	2320      	movs	r3, #32
a0a007c6:	7093      	strb	r3, [r2, #2]
a0a007c8:	2328      	movs	r3, #40	@ 0x28
a0a007ca:	70d3      	strb	r3, [r2, #3]
a0a007cc:	f9b0 3018 	ldrsh.w	r3, [r0, #24]
a0a007d0:	2b00      	cmp	r3, #0
a0a007d2:	bfb7      	itett	lt
a0a007d4:	425b      	neglt	r3, r3
a0a007d6:	f102 0c04 	addge.w	ip, r2, #4
a0a007da:	242d      	movlt	r4, #45	@ 0x2d
a0a007dc:	f102 0c05 	addlt.w	ip, r2, #5
a0a007e0:	bfb8      	it	lt
a0a007e2:	7114      	strblt	r4, [r2, #4]
a0a007e4:	2b63      	cmp	r3, #99	@ 0x63
a0a007e6:	f340 8133 	ble.w	a0a00a50 <film_apply+0x4e0>
a0a007ea:	f248 521f 	movw	r2, #34079	@ 0x851f
a0a007ee:	f2c5 12eb 	movt	r2, #20971	@ 0x51eb
a0a007f2:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a007f6:	fba2 4203 	umull	r4, r2, r2, r3
a0a007fa:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a007fe:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00802:	0952      	lsrs	r2, r2, #5
a0a00804:	fb0e 3e12 	mls	lr, lr, r2, r3
a0a00808:	f102 0330 	add.w	r3, r2, #48	@ 0x30
a0a0080c:	fba4 240e 	umull	r2, r4, r4, lr
a0a00810:	4662      	mov	r2, ip
a0a00812:	08e4      	lsrs	r4, r4, #3
a0a00814:	f802 3b02 	strb.w	r3, [r2], #2
a0a00818:	230a      	movs	r3, #10
a0a0081a:	fb03 e314 	mls	r3, r3, r4, lr
a0a0081e:	3430      	adds	r4, #48	@ 0x30
a0a00820:	f88c 4001 	strb.w	r4, [ip, #1]
a0a00824:	3330      	adds	r3, #48	@ 0x30
a0a00826:	7013      	strb	r3, [r2, #0]
a0a00828:	232c      	movs	r3, #44	@ 0x2c
a0a0082a:	7053      	strb	r3, [r2, #1]
a0a0082c:	f9b0 301a 	ldrsh.w	r3, [r0, #26]
a0a00830:	2b00      	cmp	r3, #0
a0a00832:	bfb7      	itett	lt
a0a00834:	425b      	neglt	r3, r3
a0a00836:	f102 0c02 	addge.w	ip, r2, #2
a0a0083a:	242d      	movlt	r4, #45	@ 0x2d
a0a0083c:	f102 0c03 	addlt.w	ip, r2, #3
a0a00840:	bfb8      	it	lt
a0a00842:	7094      	strblt	r4, [r2, #2]
a0a00844:	2b63      	cmp	r3, #99	@ 0x63
a0a00846:	f340 80ca 	ble.w	a0a009de <film_apply+0x46e>
a0a0084a:	f248 521f 	movw	r2, #34079	@ 0x851f
a0a0084e:	f2c5 12eb 	movt	r2, #20971	@ 0x51eb
a0a00852:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a00856:	fba2 4203 	umull	r4, r2, r2, r3
a0a0085a:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a0085e:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00862:	0952      	lsrs	r2, r2, #5
a0a00864:	fb0e 3e12 	mls	lr, lr, r2, r3
a0a00868:	f102 0330 	add.w	r3, r2, #48	@ 0x30
a0a0086c:	fba4 240e 	umull	r2, r4, r4, lr
a0a00870:	4662      	mov	r2, ip
a0a00872:	08e4      	lsrs	r4, r4, #3
a0a00874:	f802 3b02 	strb.w	r3, [r2], #2
a0a00878:	230a      	movs	r3, #10
a0a0087a:	fb03 e314 	mls	r3, r3, r4, lr
a0a0087e:	3430      	adds	r4, #48	@ 0x30
a0a00880:	f88c 4001 	strb.w	r4, [ip, #1]
a0a00884:	3330      	adds	r3, #48	@ 0x30
a0a00886:	7013      	strb	r3, [r2, #0]
a0a00888:	232c      	movs	r3, #44	@ 0x2c
a0a0088a:	7053      	strb	r3, [r2, #1]
a0a0088c:	f9b0 301c 	ldrsh.w	r3, [r0, #28]
a0a00890:	2b00      	cmp	r3, #0
a0a00892:	bfb7      	itett	lt
a0a00894:	425b      	neglt	r3, r3
a0a00896:	f102 0c02 	addge.w	ip, r2, #2
a0a0089a:	242d      	movlt	r4, #45	@ 0x2d
a0a0089c:	f102 0c03 	addlt.w	ip, r2, #3
a0a008a0:	bfb8      	it	lt
a0a008a2:	7094      	strblt	r4, [r2, #2]
a0a008a4:	2b63      	cmp	r3, #99	@ 0x63
a0a008a6:	f340 8088 	ble.w	a0a009ba <film_apply+0x44a>
a0a008aa:	f248 521f 	movw	r2, #34079	@ 0x851f
a0a008ae:	f2c5 12eb 	movt	r2, #20971	@ 0x51eb
a0a008b2:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a008b6:	fba2 4203 	umull	r4, r2, r2, r3
a0a008ba:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a008be:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a008c2:	0952      	lsrs	r2, r2, #5
a0a008c4:	fb0e 3e12 	mls	lr, lr, r2, r3
a0a008c8:	f102 0330 	add.w	r3, r2, #48	@ 0x30
a0a008cc:	fba4 240e 	umull	r2, r4, r4, lr
a0a008d0:	4662      	mov	r2, ip
a0a008d2:	08e4      	lsrs	r4, r4, #3
a0a008d4:	f802 3b02 	strb.w	r3, [r2], #2
a0a008d8:	230a      	movs	r3, #10
a0a008da:	fb03 e314 	mls	r3, r3, r4, lr
a0a008de:	3430      	adds	r4, #48	@ 0x30
a0a008e0:	f88c 4001 	strb.w	r4, [ip, #1]
a0a008e4:	3330      	adds	r3, #48	@ 0x30
a0a008e6:	9101      	str	r1, [sp, #4]
a0a008e8:	7013      	strb	r3, [r2, #0]
a0a008ea:	4604      	mov	r4, r0
a0a008ec:	2329      	movs	r3, #41	@ 0x29
a0a008ee:	a802      	add	r0, sp, #8
a0a008f0:	7053      	strb	r3, [r2, #1]
a0a008f2:	2300      	movs	r3, #0
a0a008f4:	7093      	strb	r3, [r2, #2]
a0a008f6:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a008fa:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a008fe:	4798      	blx	r3
a0a00900:	9901      	ldr	r1, [sp, #4]
a0a00902:	2900      	cmp	r1, #0
a0a00904:	f000 80f0 	beq.w	a0a00ae8 <film_apply+0x578>
a0a00908:	f241 2294 	movw	r2, #4756	@ 0x1294
a0a0090c:	f2ca 02a0 	movt	r2, #41120	@ 0xa0a0
a0a00910:	ab02      	add	r3, sp, #8
a0a00912:	f10d 0c0f 	add.w	ip, sp, #15
a0a00916:	2065      	movs	r0, #101	@ 0x65
a0a00918:	f803 0b01 	strb.w	r0, [r3], #1
a0a0091c:	f812 0f01 	ldrb.w	r0, [r2, #1]!
a0a00920:	4563      	cmp	r3, ip
a0a00922:	d1f9      	bne.n	a0a00918 <film_apply+0x3a8>
a0a00924:	780a      	ldrb	r2, [r1, #0]
a0a00926:	b12a      	cbz	r2, a0a00934 <film_apply+0x3c4>
a0a00928:	f803 2b01 	strb.w	r2, [r3], #1
a0a0092c:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00930:	2a00      	cmp	r2, #0
a0a00932:	d1f9      	bne.n	a0a00928 <film_apply+0x3b8>
a0a00934:	6a21      	ldr	r1, [r4, #32]
a0a00936:	2220      	movs	r2, #32
a0a00938:	f803 2b01 	strb.w	r2, [r3], #1
a0a0093c:	780a      	ldrb	r2, [r1, #0]
a0a0093e:	b12a      	cbz	r2, a0a0094c <film_apply+0x3dc>
a0a00940:	f803 2b01 	strb.w	r2, [r3], #1
a0a00944:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00948:	2a00      	cmp	r2, #0
a0a0094a:	d1f9      	bne.n	a0a00940 <film_apply+0x3d0>
a0a0094c:	6a61      	ldr	r1, [r4, #36]	@ 0x24
a0a0094e:	2220      	movs	r2, #32
a0a00950:	f803 2b01 	strb.w	r2, [r3], #1
a0a00954:	780a      	ldrb	r2, [r1, #0]
a0a00956:	b12a      	cbz	r2, a0a00964 <film_apply+0x3f4>
a0a00958:	f803 2b01 	strb.w	r2, [r3], #1
a0a0095c:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00960:	2a00      	cmp	r2, #0
a0a00962:	d1f9      	bne.n	a0a00958 <film_apply+0x3e8>
a0a00964:	4614      	mov	r4, r2
a0a00966:	701a      	strb	r2, [r3, #0]
a0a00968:	a802      	add	r0, sp, #8
a0a0096a:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a0096e:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00972:	4798      	blx	r3
a0a00974:	4620      	mov	r0, r4
a0a00976:	b022      	add	sp, #136	@ 0x88
a0a00978:	bd10      	pop	{r4, pc}
a0a0097a:	f248 541f 	movw	r4, #34079	@ 0x851f
a0a0097e:	f2c5 14eb 	movt	r4, #20971	@ 0x51eb
a0a00982:	f04f 0e64 	mov.w	lr, #100	@ 0x64
a0a00986:	fba4 c402 	umull	ip, r4, r4, r2
a0a0098a:	f64c 4ccd 	movw	ip, #52429	@ 0xcccd
a0a0098e:	f6cc 4ccc 	movt	ip, #52428	@ 0xcccc
a0a00992:	0964      	lsrs	r4, r4, #5
a0a00994:	fb0e 2e14 	mls	lr, lr, r4, r2
a0a00998:	f104 0230 	add.w	r2, r4, #48	@ 0x30
a0a0099c:	fbac 4c0e 	umull	r4, ip, ip, lr
a0a009a0:	461c      	mov	r4, r3
a0a009a2:	ea4f 0cdc 	mov.w	ip, ip, lsr #3
a0a009a6:	f804 2b02 	strb.w	r2, [r4], #2
a0a009aa:	220a      	movs	r2, #10
a0a009ac:	fb02 e21c 	mls	r2, r2, ip, lr
a0a009b0:	f10c 0c30 	add.w	ip, ip, #48	@ 0x30
a0a009b4:	f883 c001 	strb.w	ip, [r3, #1]
a0a009b8:	e60c      	b.n	a0a005d4 <film_apply+0x64>
a0a009ba:	4662      	mov	r2, ip
a0a009bc:	2b09      	cmp	r3, #9
a0a009be:	dd91      	ble.n	a0a008e4 <film_apply+0x374>
a0a009c0:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a009c4:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a009c8:	fba4 c403 	umull	ip, r4, r4, r3
a0a009cc:	f04f 0c0a 	mov.w	ip, #10
a0a009d0:	08e4      	lsrs	r4, r4, #3
a0a009d2:	fb0c 3314 	mls	r3, ip, r4, r3
a0a009d6:	3430      	adds	r4, #48	@ 0x30
a0a009d8:	f802 4b01 	strb.w	r4, [r2], #1
a0a009dc:	e782      	b.n	a0a008e4 <film_apply+0x374>
a0a009de:	4662      	mov	r2, ip
a0a009e0:	2b09      	cmp	r3, #9
a0a009e2:	f77f af4f 	ble.w	a0a00884 <film_apply+0x314>
a0a009e6:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a009ea:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a009ee:	fba4 c403 	umull	ip, r4, r4, r3
a0a009f2:	f04f 0c0a 	mov.w	ip, #10
a0a009f6:	08e4      	lsrs	r4, r4, #3
a0a009f8:	fb0c 3314 	mls	r3, ip, r4, r3
a0a009fc:	3430      	adds	r4, #48	@ 0x30
a0a009fe:	f802 4b01 	strb.w	r4, [r2], #1
a0a00a02:	e73f      	b.n	a0a00884 <film_apply+0x314>
a0a00a04:	4662      	mov	r2, ip
a0a00a06:	2b09      	cmp	r3, #9
a0a00a08:	f77f ae44 	ble.w	a0a00694 <film_apply+0x124>
a0a00a0c:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a00a10:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00a14:	fba4 c403 	umull	ip, r4, r4, r3
a0a00a18:	f04f 0c0a 	mov.w	ip, #10
a0a00a1c:	08e4      	lsrs	r4, r4, #3
a0a00a1e:	fb0c 3314 	mls	r3, ip, r4, r3
a0a00a22:	3430      	adds	r4, #48	@ 0x30
a0a00a24:	f802 4b01 	strb.w	r4, [r2], #1
a0a00a28:	e634      	b.n	a0a00694 <film_apply+0x124>
a0a00a2a:	4662      	mov	r2, ip
a0a00a2c:	2b09      	cmp	r3, #9
a0a00a2e:	f77f ae01 	ble.w	a0a00634 <film_apply+0xc4>
a0a00a32:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a00a36:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00a3a:	fba4 c403 	umull	ip, r4, r4, r3
a0a00a3e:	f04f 0c0a 	mov.w	ip, #10
a0a00a42:	08e4      	lsrs	r4, r4, #3
a0a00a44:	fb0c 3314 	mls	r3, ip, r4, r3
a0a00a48:	3430      	adds	r4, #48	@ 0x30
a0a00a4a:	f802 4b01 	strb.w	r4, [r2], #1
a0a00a4e:	e5f1      	b.n	a0a00634 <film_apply+0xc4>
a0a00a50:	4662      	mov	r2, ip
a0a00a52:	2b09      	cmp	r3, #9
a0a00a54:	f77f aee6 	ble.w	a0a00824 <film_apply+0x2b4>
a0a00a58:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a00a5c:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00a60:	fba4 c403 	umull	ip, r4, r4, r3
a0a00a64:	f04f 0c0a 	mov.w	ip, #10
a0a00a68:	08e4      	lsrs	r4, r4, #3
a0a00a6a:	fb0c 3314 	mls	r3, ip, r4, r3
a0a00a6e:	3430      	adds	r4, #48	@ 0x30
a0a00a70:	f802 4b01 	strb.w	r4, [r2], #1
a0a00a74:	e6d6      	b.n	a0a00824 <film_apply+0x2b4>
a0a00a76:	4662      	mov	r2, ip
a0a00a78:	2b09      	cmp	r3, #9
a0a00a7a:	f77f ae9f 	ble.w	a0a007bc <film_apply+0x24c>
a0a00a7e:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a00a82:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00a86:	fba4 c403 	umull	ip, r4, r4, r3
a0a00a8a:	f04f 0c0a 	mov.w	ip, #10
a0a00a8e:	08e4      	lsrs	r4, r4, #3
a0a00a90:	fb0c 3314 	mls	r3, ip, r4, r3
a0a00a94:	3430      	adds	r4, #48	@ 0x30
a0a00a96:	f802 4b01 	strb.w	r4, [r2], #1
a0a00a9a:	e68f      	b.n	a0a007bc <film_apply+0x24c>
a0a00a9c:	4662      	mov	r2, ip
a0a00a9e:	2b09      	cmp	r3, #9
a0a00aa0:	f77f ae5c 	ble.w	a0a0075c <film_apply+0x1ec>
a0a00aa4:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a00aa8:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00aac:	fba4 c403 	umull	ip, r4, r4, r3
a0a00ab0:	f04f 0c0a 	mov.w	ip, #10
a0a00ab4:	08e4      	lsrs	r4, r4, #3
a0a00ab6:	fb0c 3314 	mls	r3, ip, r4, r3
a0a00aba:	3430      	adds	r4, #48	@ 0x30
a0a00abc:	f802 4b01 	strb.w	r4, [r2], #1
a0a00ac0:	e64c      	b.n	a0a0075c <film_apply+0x1ec>
a0a00ac2:	4662      	mov	r2, ip
a0a00ac4:	2b09      	cmp	r3, #9
a0a00ac6:	f77f ae19 	ble.w	a0a006fc <film_apply+0x18c>
a0a00aca:	f64c 44cd 	movw	r4, #52429	@ 0xcccd
a0a00ace:	f6cc 44cc 	movt	r4, #52428	@ 0xcccc
a0a00ad2:	fba4 c403 	umull	ip, r4, r4, r3
a0a00ad6:	f04f 0c0a 	mov.w	ip, #10
a0a00ada:	08e4      	lsrs	r4, r4, #3
a0a00adc:	fb0c 3314 	mls	r3, ip, r4, r3
a0a00ae0:	3430      	adds	r4, #48	@ 0x30
a0a00ae2:	f802 4b01 	strb.w	r4, [r2], #1
a0a00ae6:	e609      	b.n	a0a006fc <film_apply+0x18c>
a0a00ae8:	68a1      	ldr	r1, [r4, #8]
a0a00aea:	e70d      	b.n	a0a00908 <film_apply+0x398>
a0a00aec:	f04f 30ff 	mov.w	r0, #4294967295	@ 0xffffffff
a0a00af0:	4770      	bx	lr
a0a00af2:	bf00      	nop

a0a00af4 <cfw_log>:
a0a00af4:	b570      	push	{r4, r5, r6, lr}
a0a00af6:	f24c 730f 	movw	r3, #50959	@ 0xc70f
a0a00afa:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00afe:	4605      	mov	r5, r0
a0a00b00:	f240 3102 	movw	r1, #770	@ 0x302
a0a00b04:	f241 4038 	movw	r0, #5176	@ 0x1438
a0a00b08:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00b0c:	4798      	blx	r3
a0a00b0e:	b1d0      	cbz	r0, a0a00b46 <cfw_log+0x52>
a0a00b10:	782a      	ldrb	r2, [r5, #0]
a0a00b12:	4604      	mov	r4, r0
a0a00b14:	b142      	cbz	r2, a0a00b28 <cfw_log+0x34>
a0a00b16:	462b      	mov	r3, r5
a0a00b18:	4618      	mov	r0, r3
a0a00b1a:	f813 1f01 	ldrb.w	r1, [r3, #1]!
a0a00b1e:	2900      	cmp	r1, #0
a0a00b20:	d1fa      	bne.n	a0a00b18 <cfw_log+0x24>
a0a00b22:	f1c5 0201 	rsb	r2, r5, #1
a0a00b26:	4402      	add	r2, r0
a0a00b28:	4629      	mov	r1, r5
a0a00b2a:	4620      	mov	r0, r4
a0a00b2c:	f64c 434b 	movw	r3, #52299	@ 0xcc4b
a0a00b30:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00b34:	4798      	blx	r3
a0a00b36:	4620      	mov	r0, r4
a0a00b38:	f64c 1305 	movw	r3, #51461	@ 0xc905
a0a00b3c:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00b40:	e8bd 4070 	ldmia.w	sp!, {r4, r5, r6, lr}
a0a00b44:	4718      	bx	r3
a0a00b46:	bd70      	pop	{r4, r5, r6, pc}

a0a00b48 <hook_efx_hold>:
a0a00b48:	b500      	push	{lr}
a0a00b4a:	2300      	movs	r3, #0
a0a00b4c:	f240 10a7 	movw	r0, #423	@ 0x1a7
a0a00b50:	b083      	sub	sp, #12
a0a00b52:	a901      	add	r1, sp, #4
a0a00b54:	9301      	str	r3, [sp, #4]
a0a00b56:	f241 1351 	movw	r3, #4433	@ 0x1151
a0a00b5a:	f2ca 030e 	movt	r3, #40974	@ 0xa00e
a0a00b5e:	4798      	blx	r3
a0a00b60:	9b01      	ldr	r3, [sp, #4]
a0a00b62:	2801      	cmp	r0, #1
a0a00b64:	bf18      	it	ne
a0a00b66:	2b01      	cmpne	r3, #1
a0a00b68:	d109      	bne.n	a0a00b7e <hook_efx_hold+0x36>
a0a00b6a:	f241 4048 	movw	r0, #5192	@ 0x1448
a0a00b6e:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00b72:	f7ff ffbf 	bl	a0a00af4 <cfw_log>
a0a00b76:	f7ff fbd1 	bl	a0a0031c <ui_toggle_menu>
a0a00b7a:	b003      	add	sp, #12
a0a00b7c:	bd00      	pop	{pc}
a0a00b7e:	f241 4090 	movw	r0, #5264	@ 0x1490
a0a00b82:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00b86:	f7ff ffb5 	bl	a0a00af4 <cfw_log>
a0a00b8a:	f241 7393 	movw	r3, #6035	@ 0x1793
a0a00b8e:	f2ca 0339 	movt	r3, #41017	@ 0xa039
a0a00b92:	4798      	blx	r3
a0a00b94:	b003      	add	sp, #12
a0a00b96:	bd00      	pop	{pc}

a0a00b98 <hooks_init>:
a0a00b98:	f44f 4c10 	mov.w	ip, #36864	@ 0x9000
a0a00b9c:	f2ca 0c3e 	movt	ip, #41022	@ 0xa03e
a0a00ba0:	f640 3249 	movw	r2, #2889	@ 0xb49
a0a00ba4:	f2ca 02a0 	movt	r2, #41120	@ 0xa0a0
a0a00ba8:	f241 40d8 	movw	r0, #5336	@ 0x14d8
a0a00bac:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00bb0:	f243 5322 	movw	r3, #13602	@ 0x3522
a0a00bb4:	f2ca 034f 	movt	r3, #41039	@ 0xa04f
a0a00bb8:	f042 0101 	orr.w	r1, r2, #1
a0a00bbc:	2200      	movs	r2, #0
a0a00bbe:	f8cc 1984 	str.w	r1, [ip, #2436]	@ 0x984
a0a00bc2:	214f      	movs	r1, #79	@ 0x4f
a0a00bc4:	8019      	strh	r1, [r3, #0]
a0a00bc6:	3201      	adds	r2, #1
a0a00bc8:	f810 1f01 	ldrb.w	r1, [r0, #1]!
a0a00bcc:	3302      	adds	r3, #2
a0a00bce:	b109      	cbz	r1, a0a00bd4 <hooks_init+0x3c>
a0a00bd0:	2a10      	cmp	r2, #16
a0a00bd2:	d1f7      	bne.n	a0a00bc4 <hooks_init+0x2c>
a0a00bd4:	f649 2391 	movw	r3, #39569	@ 0x9a91
a0a00bd8:	f2cd 0327 	movt	r3, #53287	@ 0xd027
a0a00bdc:	4413      	add	r3, r2
a0a00bde:	f243 5244 	movw	r2, #13636	@ 0x3544
a0a00be2:	f2ca 024f 	movt	r2, #41039	@ 0xa04f
a0a00be6:	2100      	movs	r1, #0
a0a00be8:	005b      	lsls	r3, r3, #1
a0a00bea:	f823 1b02 	strh.w	r1, [r3], #2
a0a00bee:	4293      	cmp	r3, r2
a0a00bf0:	d1fb      	bne.n	a0a00bea <hooks_init+0x52>
a0a00bf2:	4770      	bx	lr

a0a00bf4 <hooks_set_clarity_slider>:
a0a00bf4:	b308      	cbz	r0, a0a00c3a <hooks_set_clarity_slider+0x46>
a0a00bf6:	f241 40e8 	movw	r0, #5352	@ 0x14e8
a0a00bfa:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00bfe:	f640 039e 	movw	r3, #2206	@ 0x89e
a0a00c02:	f2ca 034f 	movt	r3, #41039	@ 0xa04f
a0a00c06:	2200      	movs	r2, #0
a0a00c08:	2143      	movs	r1, #67	@ 0x43
a0a00c0a:	8019      	strh	r1, [r3, #0]
a0a00c0c:	3201      	adds	r2, #1
a0a00c0e:	f810 1f01 	ldrb.w	r1, [r0, #1]!
a0a00c12:	3302      	adds	r3, #2
a0a00c14:	b109      	cbz	r1, a0a00c1a <hooks_set_clarity_slider+0x26>
a0a00c16:	2a0a      	cmp	r2, #10
a0a00c18:	d1f7      	bne.n	a0a00c0a <hooks_set_clarity_slider+0x16>
a0a00c1a:	f248 434f 	movw	r3, #33871	@ 0x844f
a0a00c1e:	f2cd 0327 	movt	r3, #53287	@ 0xd027
a0a00c22:	4413      	add	r3, r2
a0a00c24:	f640 02b4 	movw	r2, #2228	@ 0x8b4
a0a00c28:	f2ca 024f 	movt	r2, #41039	@ 0xa04f
a0a00c2c:	2100      	movs	r1, #0
a0a00c2e:	005b      	lsls	r3, r3, #1
a0a00c30:	f823 1b02 	strh.w	r1, [r3], #2
a0a00c34:	4293      	cmp	r3, r2
a0a00c36:	d1fb      	bne.n	a0a00c30 <hooks_set_clarity_slider+0x3c>
a0a00c38:	4770      	bx	lr
a0a00c3a:	f241 41f0 	movw	r1, #5360	@ 0x14f0
a0a00c3e:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a00c42:	f640 039e 	movw	r3, #2206	@ 0x89e
a0a00c46:	f2ca 034f 	movt	r3, #41039	@ 0xa04f
a0a00c4a:	2256      	movs	r2, #86	@ 0x56
a0a00c4c:	801a      	strh	r2, [r3, #0]
a0a00c4e:	3001      	adds	r0, #1
a0a00c50:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00c54:	3302      	adds	r3, #2
a0a00c56:	b10a      	cbz	r2, a0a00c5c <hooks_set_clarity_slider+0x68>
a0a00c58:	280a      	cmp	r0, #10
a0a00c5a:	d1f7      	bne.n	a0a00c4c <hooks_set_clarity_slider+0x58>
a0a00c5c:	f248 434f 	movw	r3, #33871	@ 0x844f
a0a00c60:	f2cd 0327 	movt	r3, #53287	@ 0xd027
a0a00c64:	4403      	add	r3, r0
a0a00c66:	f640 02b4 	movw	r2, #2228	@ 0x8b4
a0a00c6a:	f2ca 024f 	movt	r2, #41039	@ 0xa04f
a0a00c6e:	2100      	movs	r1, #0
a0a00c70:	005b      	lsls	r3, r3, #1
a0a00c72:	f823 1b02 	strh.w	r1, [r3], #2
a0a00c76:	4293      	cmp	r3, r2
a0a00c78:	d1fb      	bne.n	a0a00c72 <hooks_set_clarity_slider+0x7e>
a0a00c7a:	4770      	bx	lr

a0a00c7c <hooks_set_scurve_contrast>:
a0a00c7c:	b140      	cbz	r0, a0a00c90 <hooks_set_scurve_contrast+0x14>
a0a00c7e:	f241 40fc 	movw	r0, #5372	@ 0x14fc
a0a00c82:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00c86:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a00c8a:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00c8e:	4718      	bx	r3
a0a00c90:	f241 501c 	movw	r0, #5404	@ 0x151c
a0a00c94:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00c98:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a00c9c:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00ca0:	4718      	bx	r3
a0a00ca2:	bf00      	nop

a0a00ca4 <hooks_set_auto_iso_floor>:
a0a00ca4:	b140      	cbz	r0, a0a00cb8 <hooks_set_auto_iso_floor+0x14>
a0a00ca6:	f241 5038 	movw	r0, #5432	@ 0x1538
a0a00caa:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00cae:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a00cb2:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00cb6:	4718      	bx	r3
a0a00cb8:	f241 5054 	movw	r0, #5460	@ 0x1554
a0a00cbc:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00cc0:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a00cc4:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00cc8:	4718      	bx	r3
a0a00cca:	bf00      	nop

a0a00ccc <hooks_set_fine_grain_nr>:
a0a00ccc:	b140      	cbz	r0, a0a00ce0 <hooks_set_fine_grain_nr+0x14>
a0a00cce:	f241 5070 	movw	r0, #5488	@ 0x1570
a0a00cd2:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00cd6:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a00cda:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00cde:	4718      	bx	r3
a0a00ce0:	f241 5088 	movw	r0, #5512	@ 0x1588
a0a00ce4:	f2ca 00a0 	movt	r0, #41120	@ 0xa0a0
a0a00ce8:	f24a 2375 	movw	r3, #41589	@ 0xa275
a0a00cec:	f2ca 0302 	movt	r3, #40962	@ 0xa002
a0a00cf0:	4718      	bx	r3
a0a00cf2:	bf00      	nop

a0a00cf4 <hooks_patch_slot_name>:
a0a00cf4:	2803      	cmp	r0, #3
a0a00cf6:	d826      	bhi.n	a0a00d46 <hooks_patch_slot_name+0x52>
a0a00cf8:	e8df f000 	tbb	[pc, r0]
a0a00cfc:	6a482602 	.word	0x6a482602
a0a00d00:	2900      	cmp	r1, #0
a0a00d02:	f000 8085 	beq.w	a0a00e10 <hooks_patch_slot_name+0x11c>
a0a00d06:	780a      	ldrb	r2, [r1, #0]
a0a00d08:	2a00      	cmp	r2, #0
a0a00d0a:	f000 809b 	beq.w	a0a00e44 <hooks_patch_slot_name+0x150>
a0a00d0e:	f240 63a6 	movw	r3, #1702	@ 0x6a6
a0a00d12:	f2ca 034f 	movt	r3, #41039	@ 0xa04f
a0a00d16:	2000      	movs	r0, #0
a0a00d18:	801a      	strh	r2, [r3, #0]
a0a00d1a:	3001      	adds	r0, #1
a0a00d1c:	3302      	adds	r3, #2
a0a00d1e:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00d22:	b10a      	cbz	r2, a0a00d28 <hooks_patch_slot_name+0x34>
a0a00d24:	280d      	cmp	r0, #13
a0a00d26:	d1f7      	bne.n	a0a00d18 <hooks_patch_slot_name+0x24>
a0a00d28:	f248 3353 	movw	r3, #33619	@ 0x8353
a0a00d2c:	f2cd 0327 	movt	r3, #53287	@ 0xd027
a0a00d30:	4403      	add	r3, r0
a0a00d32:	f240 62c2 	movw	r2, #1730	@ 0x6c2
a0a00d36:	f2ca 024f 	movt	r2, #41039	@ 0xa04f
a0a00d3a:	2100      	movs	r1, #0
a0a00d3c:	005b      	lsls	r3, r3, #1
a0a00d3e:	f823 1b02 	strh.w	r1, [r3], #2
a0a00d42:	4293      	cmp	r3, r2
a0a00d44:	d1fb      	bne.n	a0a00d3e <hooks_patch_slot_name+0x4a>
a0a00d46:	4770      	bx	lr
a0a00d48:	2900      	cmp	r1, #0
a0a00d4a:	d073      	beq.n	a0a00e34 <hooks_patch_slot_name+0x140>
a0a00d4c:	780a      	ldrb	r2, [r1, #0]
a0a00d4e:	2a00      	cmp	r2, #0
a0a00d50:	d07a      	beq.n	a0a00e48 <hooks_patch_slot_name+0x154>
a0a00d52:	f240 7332 	movw	r3, #1842	@ 0x732
a0a00d56:	f2ca 034f 	movt	r3, #41039	@ 0xa04f
a0a00d5a:	2000      	movs	r0, #0
a0a00d5c:	801a      	strh	r2, [r3, #0]
a0a00d5e:	3001      	adds	r0, #1
a0a00d60:	3302      	adds	r3, #2
a0a00d62:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00d66:	b10a      	cbz	r2, a0a00d6c <hooks_patch_slot_name+0x78>
a0a00d68:	2805      	cmp	r0, #5
a0a00d6a:	d1f7      	bne.n	a0a00d5c <hooks_patch_slot_name+0x68>
a0a00d6c:	f248 3399 	movw	r3, #33689	@ 0x8399
a0a00d70:	f2cd 0327 	movt	r3, #53287	@ 0xd027
a0a00d74:	4403      	add	r3, r0
a0a00d76:	f240 723e 	movw	r2, #1854	@ 0x73e
a0a00d7a:	f2ca 024f 	movt	r2, #41039	@ 0xa04f
a0a00d7e:	2100      	movs	r1, #0
a0a00d80:	005b      	lsls	r3, r3, #1
a0a00d82:	f823 1b02 	strh.w	r1, [r3], #2
a0a00d86:	4293      	cmp	r3, r2
a0a00d88:	d1fb      	bne.n	a0a00d82 <hooks_patch_slot_name+0x8e>
a0a00d8a:	4770      	bx	lr
a0a00d8c:	2900      	cmp	r1, #0
a0a00d8e:	d04b      	beq.n	a0a00e28 <hooks_patch_slot_name+0x134>
a0a00d90:	780a      	ldrb	r2, [r1, #0]
a0a00d92:	2a00      	cmp	r2, #0
a0a00d94:	d05a      	beq.n	a0a00e4c <hooks_patch_slot_name+0x158>
a0a00d96:	f240 63fa 	movw	r3, #1786	@ 0x6fa
a0a00d9a:	f2ca 034f 	movt	r3, #41039	@ 0xa04f
a0a00d9e:	2000      	movs	r0, #0
a0a00da0:	801a      	strh	r2, [r3, #0]
a0a00da2:	3001      	adds	r0, #1
a0a00da4:	3302      	adds	r3, #2
a0a00da6:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00daa:	b10a      	cbz	r2, a0a00db0 <hooks_patch_slot_name+0xbc>
a0a00dac:	280d      	cmp	r0, #13
a0a00dae:	d1f7      	bne.n	a0a00da0 <hooks_patch_slot_name+0xac>
a0a00db0:	f248 337d 	movw	r3, #33661	@ 0x837d
a0a00db4:	f2cd 0327 	movt	r3, #53287	@ 0xd027
a0a00db8:	4403      	add	r3, r0
a0a00dba:	f240 7216 	movw	r2, #1814	@ 0x716
a0a00dbe:	f2ca 024f 	movt	r2, #41039	@ 0xa04f
a0a00dc2:	2100      	movs	r1, #0
a0a00dc4:	005b      	lsls	r3, r3, #1
a0a00dc6:	f823 1b02 	strh.w	r1, [r3], #2
a0a00dca:	4293      	cmp	r3, r2
a0a00dcc:	d1fb      	bne.n	a0a00dc6 <hooks_patch_slot_name+0xd2>
a0a00dce:	4770      	bx	lr
a0a00dd0:	b321      	cbz	r1, a0a00e1c <hooks_patch_slot_name+0x128>
a0a00dd2:	780a      	ldrb	r2, [r1, #0]
a0a00dd4:	b3a2      	cbz	r2, a0a00e40 <hooks_patch_slot_name+0x14c>
a0a00dd6:	f240 63de 	movw	r3, #1758	@ 0x6de
a0a00dda:	f2ca 034f 	movt	r3, #41039	@ 0xa04f
a0a00dde:	2000      	movs	r0, #0
a0a00de0:	801a      	strh	r2, [r3, #0]
a0a00de2:	3001      	adds	r0, #1
a0a00de4:	3302      	adds	r3, #2
a0a00de6:	f811 2f01 	ldrb.w	r2, [r1, #1]!
a0a00dea:	b10a      	cbz	r2, a0a00df0 <hooks_patch_slot_name+0xfc>
a0a00dec:	280d      	cmp	r0, #13
a0a00dee:	d1f7      	bne.n	a0a00de0 <hooks_patch_slot_name+0xec>
a0a00df0:	f248 336f 	movw	r3, #33647	@ 0x836f
a0a00df4:	f2cd 0327 	movt	r3, #53287	@ 0xd027
a0a00df8:	4403      	add	r3, r0
a0a00dfa:	f240 62fa 	movw	r2, #1786	@ 0x6fa
a0a00dfe:	f2ca 024f 	movt	r2, #41039	@ 0xa04f
a0a00e02:	2100      	movs	r1, #0
a0a00e04:	005b      	lsls	r3, r3, #1
a0a00e06:	f823 1b02 	strh.w	r1, [r3], #2
a0a00e0a:	4293      	cmp	r3, r2
a0a00e0c:	d1fb      	bne.n	a0a00e06 <hooks_patch_slot_name+0x112>
a0a00e0e:	4770      	bx	lr
a0a00e10:	f241 214c 	movw	r1, #4684	@ 0x124c
a0a00e14:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a00e18:	2243      	movs	r2, #67	@ 0x43
a0a00e1a:	e778      	b.n	a0a00d0e <hooks_patch_slot_name+0x1a>
a0a00e1c:	f241 2124 	movw	r1, #4644	@ 0x1224
a0a00e20:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a00e24:	2250      	movs	r2, #80	@ 0x50
a0a00e26:	e7d6      	b.n	a0a00dd6 <hooks_patch_slot_name+0xe2>
a0a00e28:	f241 2134 	movw	r1, #4660	@ 0x1234
a0a00e2c:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a00e30:	2242      	movs	r2, #66	@ 0x42
a0a00e32:	e7b0      	b.n	a0a00d96 <hooks_patch_slot_name+0xa2>
a0a00e34:	f241 2144 	movw	r1, #4676	@ 0x1244
a0a00e38:	f2ca 01a0 	movt	r1, #41120	@ 0xa0a0
a0a00e3c:	2252      	movs	r2, #82	@ 0x52
a0a00e3e:	e788      	b.n	a0a00d52 <hooks_patch_slot_name+0x5e>
a0a00e40:	4610      	mov	r0, r2
a0a00e42:	e7d5      	b.n	a0a00df0 <hooks_patch_slot_name+0xfc>
a0a00e44:	4610      	mov	r0, r2
a0a00e46:	e76f      	b.n	a0a00d28 <hooks_patch_slot_name+0x34>
a0a00e48:	4610      	mov	r0, r2
a0a00e4a:	e78f      	b.n	a0a00d6c <hooks_patch_slot_name+0x78>
a0a00e4c:	4610      	mov	r0, r2
a0a00e4e:	e7af      	b.n	a0a00db0 <hooks_patch_slot_name+0xbc>
a0a00e50:	2d2d3e20 	.word	0x2d2d3e20
a0a00e54:	4346573a 	.word	0x4346573a
a0a00e58:	20636677 	.word	0x20636677
a0a00e5c:	5f6d6169 	.word	0x5f6d6169
a0a00e60:	6e282920 	.word	0x6e282920
a0a00e64:	656e7465 	.word	0x656e7465
a0a00e68:	72656420 	.word	0x72656420
a0a00e6c:	73756363 	.word	0x73756363
a0a00e70:	65737366 	.word	0x65737366
a0a00e74:	756c6c79 	.word	0x756c6c79
a0a00e78:	20617420 	.word	0x20617420
a0a00e7c:	30784130 	.word	0x30784130
a0a00e80:	41303030 	.word	0x41303030
a0a00e84:	3030210a 	.word	0x3030210a
a0a00e88:	00000000 	.word	0x00000000
a0a00e8c:	2d2d3e20 	.word	0x2d2d3e20
a0a00e90:	4346573a 	.word	0x4346573a
a0a00e94:	2075695f 	.word	0x2075695f
a0a00e98:	696e6974 	.word	0x696e6974
a0a00e9c:	28292063 	.word	0x28292063
a0a00ea0:	6f6d706c 	.word	0x6f6d706c
a0a00ea4:	65746564 	.word	0x65746564
a0a00ea8:	2028616c 	.word	0x2028616c
a0a00eac:	6c206665 	.word	0x6c206665
a0a00eb0:	61747572 	.word	0x61747572
a0a00eb4:	6573206e 	.word	0x6573206e
a0a00eb8:	65757472 	.word	0x65757472
a0a00ebc:	616c2f6f 	.word	0x616c2f6f
a0a00ec0:	6666290a 	.word	0x6666290a
a0a00ec4:	00000000 	.word	0x00000000
a0a00ec8:	2d2d3e20 	.word	0x2d2d3e20
a0a00ecc:	4346573a 	.word	0x4346573a
a0a00ed0:	20686f6f 	.word	0x20686f6f
a0a00ed4:	6b735f69 	.word	0x6b735f69
a0a00ed8:	6e697428 	.word	0x6e697428
a0a00edc:	2920636f 	.word	0x2920636f
a0a00ee0:	6d706c65 	.word	0x6d706c65
a0a00ee4:	74656420 	.word	0x74656420
a0a00ee8:	28307841 	.word	0x28307841
a0a00eec:	30334539 	.word	0x30334539
a0a00ef0:	39383420 	.word	0x39383420
a0a00ef4:	686f6f6b 	.word	0x686f6f6b
a0a00ef8:	65642074 	.word	0x65642074
a0a00efc:	6f20686f 	.word	0x6f20686f
a0a00f00:	6f6b5f65 	.word	0x6f6b5f65
a0a00f04:	66785f68 	.word	0x66785f68
a0a00f08:	6f6c6429 	.word	0x6f6c6429
a0a00f0c:	0a000000 	.word	0x0a000000
a0a00f10:	205b4f46 	.word	0x205b4f46
a0a00f14:	465d2200 	.word	0x465d2200
a0a00f18:	205b4f4e 	.word	0x205b4f4e
a0a00f1c:	5d220000 	.word	0x5d220000
a0a00f20:	67707269 	.word	0x67707269
a0a00f24:	6e742038 	.word	0x6e742038
a0a00f28:	30200000 	.word	0x30200000
a0a00f2c:	67707269 	.word	0x67707269
a0a00f30:	6e742038 	.word	0x6e742038
a0a00f34:	30203132 	.word	0x30203132
a0a00f38:	3020223e 	.word	0x3020223e
a0a00f3c:	2046696c 	.word	0x2046696c
a0a00f40:	6d204c6f 	.word	0x6d204c6f
a0a00f44:	6f6b3a20 	.word	0x6f6b3a20
a0a00f48:	3c200000 	.word	0x3c200000
a0a00f4c:	67707269 	.word	0x67707269
a0a00f50:	6e742038 	.word	0x6e742038
a0a00f54:	30203132 	.word	0x30203132
a0a00f58:	30202220 	.word	0x30202220
a0a00f5c:	2046696c 	.word	0x2046696c
a0a00f60:	6d204c6f 	.word	0x6d204c6f
a0a00f64:	6f6b3a20 	.word	0x6f6b3a20
a0a00f68:	3c200000 	.word	0x3c200000
a0a00f6c:	67707269 	.word	0x67707269
a0a00f70:	6e742038 	.word	0x6e742038
a0a00f74:	30203137 	.word	0x30203137
a0a00f78:	3020223e 	.word	0x3020223e
a0a00f7c:	20417373 	.word	0x20417373
a0a00f80:	69676e20 	.word	0x69676e20
a0a00f84:	546f3a20 	.word	0x546f3a20
a0a00f88:	3c200000 	.word	0x3c200000
a0a00f8c:	67707269 	.word	0x67707269
a0a00f90:	6e742038 	.word	0x6e742038
a0a00f94:	30203137 	.word	0x30203137
a0a00f98:	30202220 	.word	0x30202220
a0a00f9c:	20417373 	.word	0x20417373
a0a00fa0:	69676e20 	.word	0x69676e20
a0a00fa4:	546f3a20 	.word	0x546f3a20
a0a00fa8:	3c200000 	.word	0x3c200000
a0a00fac:	556e6b6e 	.word	0x556e6b6e
a0a00fb0:	6f776e00 	.word	0x6f776e00
a0a00fb4:	67646576 	.word	0x67646576
a0a00fb8:	69636520 	.word	0x69636520
a0a00fbc:	6c636400 	.word	0x6c636400
a0a00fc0:	6770656e 	.word	0x6770656e
a0a00fc4:	20776869 	.word	0x20776869
a0a00fc8:	74652062 	.word	0x74652062
a0a00fcc:	6c61636b 	.word	0x6c61636b
a0a00fd0:	00000000 	.word	0x00000000
a0a00fd4:	67726563 	.word	0x67726563
a0a00fd8:	74203530 	.word	0x74203530
a0a00fdc:	20333020 	.word	0x20333020
a0a00fe0:	35393020 	.word	0x35393020
a0a00fe4:	34353000 	.word	0x34353000
a0a00fe8:	67707269 	.word	0x67707269
a0a00fec:	6e742037 	.word	0x6e742037
a0a00ff0:	30203530 	.word	0x30203530
a0a00ff4:	20225b2a 	.word	0x20225b2a
a0a00ff8:	2046494c 	.word	0x2046494c
a0a00ffc:	4d205245 	.word	0x4d205245
a0a01000:	43495045 	.word	0x43495045
a0a01004:	53202a5d 	.word	0x53202a5d
a0a01008:	20202020 	.word	0x20202020
a0a0100c:	20205b53 	.word	0x20205b53
a0a01010:	45545449 	.word	0x45545449
a0a01014:	4e47535d 	.word	0x4e47535d
a0a01018:	22000000 	.word	0x22000000
a0a0101c:	67707269 	.word	0x67707269
a0a01020:	6e742037 	.word	0x6e742037
a0a01024:	30203530 	.word	0x30203530
a0a01028:	2022205b 	.word	0x2022205b
a0a0102c:	46494c4d 	.word	0x46494c4d
a0a01030:	20524543 	.word	0x20524543
a0a01034:	49504553 	.word	0x49504553
a0a01038:	5d202020 	.word	0x5d202020
a0a0103c:	2020205b 	.word	0x2020205b
a0a01040:	2a205345 	.word	0x2a205345
a0a01044:	5454494e 	.word	0x5454494e
a0a01048:	4753202a 	.word	0x4753202a
a0a0104c:	5d220000 	.word	0x5d220000
a0a01050:	676c696e 	.word	0x676c696e
a0a01054:	65203730 	.word	0x65203730
a0a01058:	20373520 	.word	0x20373520
a0a0105c:	35373020 	.word	0x35373020
a0a01060:	37350000 	.word	0x37350000
a0a01064:	67707269 	.word	0x67707269
a0a01068:	6e742038 	.word	0x6e742038
a0a0106c:	30203233 	.word	0x30203233
a0a01070:	3020223e 	.word	0x3020223e
a0a01074:	205b2041 	.word	0x205b2041
a0a01078:	50504c59 	.word	0x50504c59
a0a0107c:	2046494c 	.word	0x2046494c
a0a01080:	4d20544f 	.word	0x4d20544f
a0a01084:	20534c4f 	.word	0x20534c4f
a0a01088:	5420284f 	.word	0x5420284f
a0a0108c:	4b29205d 	.word	0x4b29205d
a0a01090:	22000000 	.word	0x22000000
a0a01094:	67707269 	.word	0x67707269
a0a01098:	6e742038 	.word	0x6e742038
a0a0109c:	30203233 	.word	0x30203233
a0a010a0:	30202220 	.word	0x30202220
a0a010a4:	205b2041 	.word	0x205b2041
a0a010a8:	50504c59 	.word	0x50504c59
a0a010ac:	2046494c 	.word	0x2046494c
a0a010b0:	4d20544f 	.word	0x4d20544f
a0a010b4:	20534c4f 	.word	0x20534c4f
a0a010b8:	54205d22 	.word	0x54205d22
a0a010bc:	00000000 	.word	0x00000000
a0a010c0:	496e2d43 	.word	0x496e2d43
a0a010c4:	616d6572 	.word	0x616d6572
a0a010c8:	6120436c 	.word	0x6120436c
a0a010cc:	61726974 	.word	0x61726974
a0a010d0:	7920536c 	.word	0x7920536c
a0a010d4:	69646572 	.word	0x69646572
a0a010d8:	203a0000 	.word	0x203a0000
a0a010dc:	536d6172 	.word	0x536d6172
a0a010e0:	7420532d 	.word	0x7420532d
a0a010e4:	43757276 	.word	0x43757276
a0a010e8:	6520436f 	.word	0x6520436f
a0a010ec:	6e747261 	.word	0x6e747261
a0a010f0:	73742020 	.word	0x73742020
a0a010f4:	203a0000 	.word	0x203a0000
a0a010f8:	4175746f 	.word	0x4175746f
a0a010fc:	2d49534f 	.word	0x2d49534f
a0a01100:	20312f31 	.word	0x20312f31
a0a01104:	32357320 	.word	0x32357320
a0a01108:	466c6f6f 	.word	0x466c6f6f
a0a0110c:	72202020 	.word	0x72202020
a0a01110:	203a0000 	.word	0x203a0000
a0a01114:	48696768 	.word	0x48696768
a0a01118:	2d49534f 	.word	0x2d49534f
a0a0111c:	20446574 	.word	0x20446574
a0a01120:	61696c20 	.word	0x61696c20
a0a01124:	4e522020 	.word	0x4e522020
a0a01128:	20202020 	.word	0x20202020
a0a0112c:	203a0000 	.word	0x203a0000
a0a01130:	36353a32 	.word	0x36353a32
a0a01134:	34205850 	.word	0x34205850
a0a01138:	616e2046 	.word	0x616e2046
a0a0113c:	72616d65 	.word	0x72616d65
a0a01140:	204c696e 	.word	0x204c696e
a0a01144:	65732020 	.word	0x65732020
a0a01148:	203a0000 	.word	0x203a0000
a0a0114c:	676c696e 	.word	0x676c696e
a0a01150:	65203730 	.word	0x65203730
a0a01154:	20333830 	.word	0x20333830
a0a01158:	20353730 	.word	0x20353730
a0a0115c:	20333830 	.word	0x20333830
a0a01160:	00000000 	.word	0x00000000
a0a01164:	67707269 	.word	0x67707269
a0a01168:	6e742038 	.word	0x6e742038
a0a0116c:	30203430 	.word	0x30203430
a0a01170:	3020225b 	.word	0x3020225b
a0a01174:	55502f44 	.word	0x55502f44
a0a01178:	4e5d2053 	.word	0x4e5d2053
a0a0117c:	656c6563 	.word	0x656c6563
a0a01180:	74202020 	.word	0x74202020
a0a01184:	5b4c542f 	.word	0x5b4c542f
a0a01188:	52545d20 	.word	0x52545d20
a0a0118c:	4368616e 	.word	0x4368616e
a0a01190:	67652f54 	.word	0x67652f54
a0a01194:	6f67676c 	.word	0x6f67676c
a0a01198:	65202020 	.word	0x65202020
a0a0119c:	5b4f4b5d 	.word	0x5b4f4b5d
a0a011a0:	20416374 	.word	0x20416374
a0a011a4:	696f6e22 	.word	0x696f6e22
a0a011a8:	00000000 	.word	0x00000000
a0a011ac:	2d2d3e20 	.word	0x2d2d3e20
a0a011b0:	55493a20 	.word	0x55493a20
a0a011b4:	75695f74 	.word	0x75695f74
a0a011b8:	6f67676c 	.word	0x6f67676c
a0a011bc:	655f6d65 	.word	0x655f6d65
a0a011c0:	6e752829 	.word	0x6e752829
a0a011c4:	204f5045 	.word	0x204f5045
a0a011c8:	4e454421 	.word	0x4e454421
a0a011cc:	0a000000 	.word	0x0a000000
a0a011d0:	2d2d3e20 	.word	0x2d2d3e20
a0a011d4:	55493a20 	.word	0x55493a20
a0a011d8:	75695f74 	.word	0x75695f74
a0a011dc:	6f67676c 	.word	0x6f67676c
a0a011e0:	655f6d65 	.word	0x655f6d65
a0a011e4:	6e752829 	.word	0x6e752829
a0a011e8:	20434c4f 	.word	0x20434c4f
a0a011ec:	53454421 	.word	0x53454421
a0a011f0:	0a000000 	.word	0x0a000000
a0a011f4:	67726573 	.word	0x67726573
a0a011f8:	65740000 	.word	0x65740000
a0a011fc:	676c696e 	.word	0x676c696e
a0a01200:	65203020 	.word	0x65203020
a0a01204:	31323220 	.word	0x31323220
a0a01208:	36343020 	.word	0x36343020
a0a0120c:	31323200 	.word	0x31323200
a0a01210:	676c696e 	.word	0x676c696e
a0a01214:	65203020 	.word	0x65203020
a0a01218:	33353820 	.word	0x33353820
a0a0121c:	36343020 	.word	0x36343020
a0a01220:	33353800 	.word	0x33353800
a0a01224:	506f7369 	.word	0x506f7369
a0a01228:	74697665 	.word	0x74697665
a0a0122c:	2046696c 	.word	0x2046696c
a0a01230:	6d000000 	.word	0x6d000000
a0a01234:	426c6561 	.word	0x426c6561
a0a01238:	63682042 	.word	0x63682042
a0a0123c:	79706173 	.word	0x79706173
a0a01240:	73000000 	.word	0x73000000
a0a01244:	52657472 	.word	0x52657472
a0a01248:	6f000000 	.word	0x6f000000
a0a0124c:	43726f73 	.word	0x43726f73
a0a01250:	73205072 	.word	0x73205072
a0a01254:	6f636573 	.word	0x6f636573
a0a01258:	73000000 	.word	0x73000000
a0a0125c:	4469616c 	.word	0x4469616c
a0a01260:	204d5931 	.word	0x204d5931
a0a01264:	00000000 	.word	0x00000000
a0a01268:	4469616c 	.word	0x4469616c
a0a0126c:	204d5932 	.word	0x204d5932
a0a01270:	00000000 	.word	0x00000000
a0a01274:	4469616c 	.word	0x4469616c
a0a01278:	204d5933 	.word	0x204d5933
a0a0127c:	00000000 	.word	0x00000000
a0a01280:	6d79636d 	.word	0x6d79636d
a0a01284:	74782069 	.word	0x74782069
a0a01288:	6d735f73 	.word	0x6d735f73
a0a0128c:	65743220 	.word	0x65743220
a0a01290:	28000000 	.word	0x28000000
a0a01294:	65666373 	.word	0x65666373
a0a01298:	65742000 	.word	0x65742000
a0a0129c:	706f7274 	.word	0x706f7274
a0a012a0:	72613430 	.word	0x72613430
a0a012a4:	30000000 	.word	0x30000000
a0a012a8:	4b6f6461 	.word	0x4b6f6461
a0a012ac:	6b20506f 	.word	0x6b20506f
a0a012b0:	72747261 	.word	0x72747261
a0a012b4:	20343030 	.word	0x20343030
a0a012b8:	00000000 	.word	0x00000000
a0a012bc:	6566635f 	.word	0x6566635f
a0a012c0:	706f7369 	.word	0x706f7369
a0a012c4:	46696c6d 	.word	0x46696c6d
a0a012c8:	00000000 	.word	0x00000000
a0a012cc:	36353631 	.word	0x36353631
a0a012d0:	37000000 	.word	0x37000000
a0a012d4:	7761726d 	.word	0x7761726d
a0a012d8:	00000000 	.word	0x00000000
a0a012dc:	636c6173 	.word	0x636c6173
a0a012e0:	7369636e 	.word	0x7369636e
a0a012e4:	65670000 	.word	0x65670000
a0a012e8:	46756a69 	.word	0x46756a69
a0a012ec:	20436c61 	.word	0x20436c61
a0a012f0:	73736963 	.word	0x73736963
a0a012f4:	204e6567 	.word	0x204e6567
a0a012f8:	00000000 	.word	0x00000000
a0a012fc:	6566635f 	.word	0x6566635f
a0a01300:	626c6561 	.word	0x626c6561
a0a01304:	63684279 	.word	0x63684279
a0a01308:	70617373 	.word	0x70617373
a0a0130c:	00000000 	.word	0x00000000
a0a01310:	35373532 	.word	0x35373532
a0a01314:	38000000 	.word	0x38000000
a0a01318:	636f6f6c 	.word	0x636f6f6c
a0a0131c:	00000000 	.word	0x00000000
a0a01320:	676f6c64 	.word	0x676f6c64
a0a01324:	32303000 	.word	0x32303000
a0a01328:	4b6f6461 	.word	0x4b6f6461
a0a0132c:	6b20476f 	.word	0x6b20476f
a0a01330:	6c642032 	.word	0x6c642032
a0a01334:	30300000 	.word	0x30300000
a0a01338:	6566635f 	.word	0x6566635f
a0a0133c:	72657472 	.word	0x72657472
a0a01340:	6f000000 	.word	0x6f000000
a0a01344:	36363732 	.word	0x36363732
a0a01348:	37000000 	.word	0x37000000
a0a0134c:	656b7461 	.word	0x656b7461
a0a01350:	72313030 	.word	0x72313030
a0a01354:	00000000 	.word	0x00000000
a0a01358:	4b6f6461 	.word	0x4b6f6461
a0a0135c:	6b20456b 	.word	0x6b20456b
a0a01360:	74617220 	.word	0x74617220
a0a01364:	31303000 	.word	0x31303000
a0a01368:	35373730 	.word	0x35373730
a0a0136c:	38000000 	.word	0x38000000
a0a01370:	6e6f726d 	.word	0x6e6f726d
a0a01374:	616c0000 	.word	0x616c0000
a0a01378:	70726f34 	.word	0x70726f34
a0a0137c:	30306800 	.word	0x30306800
a0a01380:	46756a69 	.word	0x46756a69
a0a01384:	2050726f 	.word	0x2050726f
a0a01388:	20343030 	.word	0x20343030
a0a0138c:	48000000 	.word	0x48000000
a0a01390:	6566635f 	.word	0x6566635f
a0a01394:	63726f73 	.word	0x63726f73
a0a01398:	7350726f 	.word	0x7350726f
a0a0139c:	63657373 	.word	0x63657373
a0a013a0:	00000000 	.word	0x00000000
a0a013a4:	35343531 	.word	0x35343531
a0a013a8:	36000000 	.word	0x36000000
a0a013ac:	76697369 	.word	0x76697369
a0a013b0:	6f6e335f 	.word	0x6f6e335f
a0a013b4:	32353064 	.word	0x32353064
a0a013b8:	00000000 	.word	0x00000000
a0a013bc:	4b6f6461 	.word	0x4b6f6461
a0a013c0:	6b205669 	.word	0x6b205669
a0a013c4:	73696f6e 	.word	0x73696f6e
a0a013c8:	33203235 	.word	0x33203235
a0a013cc:	30440000 	.word	0x30440000
a0a013d0:	36353631 	.word	0x36353631
a0a013d4:	38000000 	.word	0x38000000
a0a013d8:	74726978 	.word	0x74726978
a0a013dc:	34303000 	.word	0x34303000
a0a013e0:	4b6f6461 	.word	0x4b6f6461
a0a013e4:	6b205472 	.word	0x6b205472
a0a013e8:	692d5820 	.word	0x692d5820
a0a013ec:	34303000 	.word	0x34303000
a0a013f0:	6566635f 	.word	0x6566635f
a0a013f4:	68696768 	.word	0x68696768
a0a013f8:	436f6e74 	.word	0x436f6e74
a0a013fc:	72617374 	.word	0x72617374
a0a01400:	00000000 	.word	0x00000000
a0a01404:	37393530 	.word	0x37393530
a0a01408:	39000000 	.word	0x39000000
a0a0140c:	62770000 	.word	0x62770000
a0a01410:	63696e65 	.word	0x63696e65
a0a01414:	7374696c 	.word	0x7374696c
a0a01418:	6c383030 	.word	0x6c383030
a0a0141c:	74000000 	.word	0x74000000
a0a01420:	43696e65 	.word	0x43696e65
a0a01424:	5374696c 	.word	0x5374696c
a0a01428:	6c203830 	.word	0x6c203830
a0a0142c:	30540000 	.word	0x30540000
a0a01430:	36363632 	.word	0x36363632
a0a01434:	38000000 	.word	0x38000000
a0a01438:	483a2f63 	.word	0x483a2f63
a0a0143c:	66775f6c 	.word	0x66775f6c
a0a01440:	6f672e74 	.word	0x6f672e74
a0a01444:	78740000 	.word	0x78740000
a0a01448:	2d2d3e20 	.word	0x2d2d3e20
a0a0144c:	45666665 	.word	0x45666665
a0a01450:	63742062 	.word	0x63742062
a0a01454:	7574746f 	.word	0x7574746f
a0a01458:	6e20686f 	.word	0x6e20686f
a0a0145c:	6c643a20 	.word	0x6c643a20
a0a01460:	274f7065 	.word	0x274f7065
a0a01464:	6e204375 	.word	0x6e204375
a0a01468:	73746f6d 	.word	0x73746f6d
a0a0146c:	204f5327 	.word	0x204f5327
a0a01470:	20616374 	.word	0x20616374
a0a01474:	69766520 	.word	0x69766520
a0a01478:	2d3e2075 	.word	0x2d3e2075
a0a0147c:	695f746f 	.word	0x695f746f
a0a01480:	67676c65 	.word	0x67676c65
a0a01484:	5f6d656e 	.word	0x5f6d656e
a0a01488:	7528290a 	.word	0x7528290a
a0a0148c:	00000000 	.word	0x00000000
a0a01490:	2d2d3e20 	.word	0x2d2d3e20
a0a01494:	45666665 	.word	0x45666665
a0a01498:	63742062 	.word	0x63742062
a0a0149c:	7574746f 	.word	0x7574746f
a0a014a0:	6e20686f 	.word	0x6e20686f
a0a014a4:	6c643a20 	.word	0x6c643a20
a0a014a8:	53746f63 	.word	0x53746f63
a0a014ac:	6b205769 	.word	0x6b205769
a0a014b0:	2d466920 	.word	0x2d466920
a0a014b4:	61637469 	.word	0x61637469
a0a014b8:	7665202d 	.word	0x7665202d
a0a014bc:	3e204752 	.word	0x3e204752
a0a014c0:	325f4f52 	.word	0x325f4f52
a0a014c4:	49475f45 	.word	0x49475f45
a0a014c8:	46585f48 	.word	0x46585f48
a0a014cc:	4f4c445f 	.word	0x4f4c445f
a0a014d0:	464e2829 	.word	0x464e2829
a0a014d4:	0a000000 	.word	0x0a000000
a0a014d8:	4f70656e 	.word	0x4f70656e
a0a014dc:	20437573 	.word	0x20437573
a0a014e0:	746f6d20 	.word	0x746f6d20
a0a014e4:	4f530000 	.word	0x4f530000
a0a014e8:	436c6172 	.word	0x436c6172
a0a014ec:	69747900 	.word	0x69747900
a0a014f0:	5669676e 	.word	0x5669676e
a0a014f4:	65747469 	.word	0x65747469
a0a014f8:	6e670000 	.word	0x6e670000
a0a014fc:	65666373 	.word	0x65666373
a0a01500:	65742065 	.word	0x65742065
a0a01504:	66635f70 	.word	0x66635f70
a0a01508:	6f736946 	.word	0x6f736946
a0a0150c:	696c6d20 	.word	0x696c6d20
a0a01510:	36353631 	.word	0x36353631
a0a01514:	37207761 	.word	0x37207761
a0a01518:	726d0000 	.word	0x726d0000
a0a0151c:	65666373 	.word	0x65666373
a0a01520:	65742065 	.word	0x65742065
a0a01524:	66635f70 	.word	0x66635f70
a0a01528:	6f736946 	.word	0x6f736946
a0a0152c:	696c6d20 	.word	0x696c6d20
a0a01530:	35353530 	.word	0x35353530
a0a01534:	39205f00 	.word	0x39205f00
a0a01538:	73766175 	.word	0x73766175
a0a0153c:	746f7365 	.word	0x746f7365
a0a01540:	74203130 	.word	0x74203130
a0a01544:	30203634 	.word	0x30203634
a0a01548:	30302031 	.word	0x30302031
a0a0154c:	2e313235 	.word	0x2e313235
a0a01550:	00000000 	.word	0x00000000
a0a01554:	73766175 	.word	0x73766175
a0a01558:	746f7365 	.word	0x746f7365
a0a0155c:	74203130 	.word	0x74203130
a0a01560:	30203235 	.word	0x30203235
a0a01564:	36303020 	.word	0x36303020
a0a01568:	6175746f 	.word	0x6175746f
a0a0156c:	00000000 	.word	0x00000000
a0a01570:	6e727365 	.word	0x6e727365
a0a01574:	7420776d 	.word	0x7420776d
a0a01578:	73203130 	.word	0x73203130
a0a0157c:	30203830 	.word	0x30203830
a0a01580:	30203136 	.word	0x30203136
a0a01584:	30300000 	.word	0x30300000
a0a01588:	6e727365 	.word	0x6e727365
a0a0158c:	7420776d 	.word	0x7420776d
a0a01590:	73203130 	.word	0x73203130
a0a01594:	30203430 	.word	0x30203430
a0a01598:	30203830 	.word	0x30203830
a0a0159c:	30000000 	.word	0x30000000

a0a015a0 <CSWTCH.31>:
a0a015a0:	a0a01224 a0a01234 a0a01244 a0a0124c     ...$...4...D...L
a0a015b0:	a0a0125c a0a01268 a0a01274              ...\...h...t

a0a015bc <g_film_catalog>:
a0a015bc:	a0a0129c a0a012a8 a0a012bc 0065ffff     .............e..
a0a015cc:	ffff0020 006c0004 fffb0016 007d0000     ... .l.......}..
a0a015dc:	a0a012cc a0a012d4 a0a012dc a0a012e8     ................
a0a015ec:	a0a012fc 0078fffd 0005ffe3 006b0008     .....x.......k..
a0a015fc:	00260018 00730000 a0a01310 a0a01318     .&...s..........
a0a0160c:	a0a01320 a0a01328 a0a01338 0068ffff     ... ...(...8.h..
a0a0161c:	fffe0016 006b0007 00020016 007b0000     .....k.......{..
a0a0162c:	a0a01344 a0a012d4 a0a0134c a0a01358     ...D.......L...X
a0a0163c:	a0a012bc 006bfffd fffe001b 006c0003     .....k.......l..
a0a0164c:	fffa0017 007e0000 a0a01368 a0a01370     .....~.....h...p
a0a0165c:	a0a01378 a0a01380 a0a01390 00640000     ...x.........d..
a0a0166c:	fffe0021 00780009 fffb0009 00790000     ...!.x.......y..
a0a0167c:	a0a013a4 a0a01318 a0a013ac a0a013bc     ................
a0a0168c:	a0a012fc 0075fffe fffd0014 007afffb     .....u.......z..
a0a0169c:	fff70007 00880000 a0a013d0 a0a01370     ...............p
a0a016ac:	a0a013d8 a0a013e0 a0a013f0 00800000     ................
a0a016bc:	00000000 00800000 00000000 00800000     ................
a0a016cc:	a0a01404 a0a0140c a0a01410 a0a01420     ............... 
a0a016dc:	a0a01390 005ffffb 0008000f 0073000c     ....._.......s..
a0a016ec:	000c0012 00870000 a0a01430 a0a01318     ...........0....
