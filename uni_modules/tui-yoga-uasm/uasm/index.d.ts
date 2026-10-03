/**
 * tui-yoga-uasm 原生插件类型声明
 * 产物名：UasmTuiYogaUasm（由 tui-yoga-uasm 经 uni-gyp module-pack 推导）
 *
 * 布局引擎（yoga 3.x）：树常驻 C 侧，增量 API + 批量布局结果。
 * 错误以异常抛出（Web/小程序为 JS Error，App 端为原生异常）。
 */

/**
 * 布局结果扁平数组元素数：[nodeId, x, y, width, height]
 */
export interface TuiYogaFrame {
	x : number
	y : number
	width : number
	height : number
}

export class TuiYogaUasm {
	/** 创建布局节点，返回节点 id */
	createNode() : number
	/** 释放单个节点（自动与父节点断开），成功 true；id 无效 false */
	freeNode(id : number) : boolean
	/** 递归释放整棵子树（含自身），成功 true；id 无效 false */
	freeTree(id : number) : boolean
	/**
	 * 挂载子节点。index 缺省时追加到末尾（index = -1 同样表示追加）；
	 * index >= 0 时插入到指定位置。成功 true。
	 */
	insertChild(parentId : number, childId : number, index ?: number) : boolean
	/** 插入到指定位置（>= 0）或末尾（-1） */
	insertChildAt(parentId : number, childId : number, index : number) : boolean
	/** 从父节点移除子节点（不释放节点本身），成功 true */
	removeChild(parentId : number, childId : number) : boolean
	/**
	 * 注册样式：propsJson 为属性 JSON（如 '{"width":"100px","paddingTop":5}'），
	 * 返回 styleId（注册一次可应用到任意多个节点）；JSON 非法抛异常。
	 */
	registerStyle(propsJson : string) : number
	/** 把注册的样式整包应用到节点，成功 true */
	applyStyle(nodeId : number, styleId : number) : boolean
	/**
	 * 单属性微更新（字符串值）：
	 * 点值 "12px"、百分比 "50%"、auto、枚举（如 "row"/"center"/"flex-start"）、null 重置。
	 */
	setStyle(nodeId : number, key : string, value : string) : boolean
	/** 单属性微更新（点值快捷方式） */
	setStyleNum(nodeId : number, key : string, value : number) : boolean
	/**
	 * 文本预测量：叶子节点（文本）尺寸由调用方测量后传入，
	 * 内部走 measure 回调返回该尺寸，父容器按此参与布局。
	 */
	setMeasuredSize(nodeId : number, width : number, height : number) : boolean
	/**
	 * 以 rootId 为根计算布局。availWidth/availHeight 为根可用空间，
	 * direction：0 = LTR，1 = RTL。成功 true；rootId 无效 false。
	 */
	calculateLayout(rootId : number, availWidth : number, availHeight : number, direction ?: number) : boolean
	/**
	 * 收集整棵树布局结果（一次跨桥）：
	 * 扁平数组 [nodeId, x, y, width, height, ...]，前序遍历，含根节点。
	 */
	collectFrames(rootId : number) : number[]
	/** 读取单节点布局结果 [nodeId, x, y, width, height]；id 无效返回空数组 */
	getFrame(nodeId : number) : number[]
	/** 节点或其子树是否有待重新布局的脏标记 */
	isDirty(nodeId : number) : boolean
}
